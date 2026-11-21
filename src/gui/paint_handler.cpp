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
#include "text_raster.h"   // XSpan, the cards' text room
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
#include <numbers>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <sys/system_properties.h>   // the chrome paint trace's property

// On-screen paint handler: on_redraw and its per-strip paint passes, and
// on_resize. The waveform plate's column bounds (the draw list's) and the
// flag-rect cache's off-screen surface (these passes blit it) are produced in
// waveform_cache.cpp. Trim paints live per frame
// (paint_trim), out of any cache.

// -- The bottom row's shaped-text tier (row 7, 2026-08-01) ------------------
//
// THE BOTTOM ROW HAS NO MONOSPACE FACE (architect 2026-10-05, after his
// ACID Pro 3.0 and Vegas screenshots: "no monospace face anywhere";
// gui_font.h): the bottom icon row's clock and its tab name are THE
// PROGRAM FACE (Regular 12 W, 24 device px on the tablet since send Z,
// every text 12 W, gui_font.h), and the render player's two time labels
// with them (architect 2026-10-26 ~12:10, "yes" to symmetry with the
// clock); every modal string and the status bar the body face, 12 W too, a
// row of its own (gui_font.h's table). The no-wiggle guarantee is the
// timestamps' fixed width, the specimen's advance.
//
// THE STANDING STATE — the queue/render status and the history walk line —
// IS THE STATUS BAR'S PROCESS LINE (architect 2026-11-19, send Z;
// status_bar_text, paint_status_bar), the body face in the text tone, no
// shadow, on the bar under the bottom icon row; notifications.h's head is
// its word-for-word record.
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
// buttons in paint_modal_dialog (one buffer left since 2026-10-29, the
// settings editor's), and
// the prompt's text-plus-labels line became a message + real buttons. The
// one-run prefix+pending shaping died with it, the pending run shaping alone
// and the published click-to-caret geometry (AppState::DialogEditorText) the
// editor's own. Since send EE a prompt is a full page, its answers on row
// 8's rows (modal_owns_bottom_row), and a line editor a sub-page of its own
// (THE VALUE SUB-PAGE below), its answers the on-screen keyboard's.)

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
    // bottom) is not blitted. A live gui_scale or key_height commit
    // (apply_gui_scale, apply_key_height; both through relayout_window)
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
//     ACT ("Drop Marker", "Go to Start", "Toggle Grid Iterations").
//     The evidence is kdenlive's own: the Title Case hover on an icon button
//     IS the QAction's name echoed back by Qt, while every tooltip kdenlive
//     writes by hand is a sentence. A LAMP'S "Toggle <Mode>" IS A NAME
//     under this — Qt names a checkable action the same way ("Show Audio
//     Thumbnails") — the lamp FACE, never the words, carrying the state.
//   * A DESCRIPTION IS SENTENCE CASE — a label, a field prefix, the SECOND
//     modifier line of a tooltip (a sentence-case clause with no terminal
//     period, a caption: "Hold for the A/B audition"; architect 2026-10-31),
//     a card, a prompt (a question), the status bar's line, and a panel's row
//     labels and prose.
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
//     Iterations"), which is also the icon-row button's own
//     ("BPM Iterations"). No reader compares any of these as strings — the
//     menus' item
//     tables were read by index (until the popups retired, 2026-10-21) —
//     so each surface is free to spell its own class.
//   * TITLE CASE SPELLING: principal words capitalized; articles,
//     conjunctions and prepositions of three letters or fewer lowercase unless
//     they lead ("Copy to Clipboard", "Load in Place", "Save and Commit", "Up
//     a Folder", "Toggle Add to Selection"); a hyphenated compound capitalizes
//     both halves ("Toggle Read-Only"); a single word is the same string
//     in either class and settles nothing.
//   * NO CARD, LABEL OR TOOLTIP NAMES A KEY (architect 2026-10-30; the
//     tooltip's rule at redesign_button_tooltip's head, app_state.h): a
//     tooltip names the act and, on its second line, the glass gesture that
//     reaches a twin ("Hold to …"). The one key text
//     is the on-screen keyboard's own caps (onscreen_keyboard.h's cap_word).
//     The dialog buttons' words are plain words, no letter underlined and
//     never a bracket (architect 2026-10-30: no keyboard stands under a
//     dialog, so an access key has no producer).
//   * ACRONYMS KEEP THEIR CAPS wherever they fall, in either case ("BPM
//     iterations work in source view", "GUI Scale", "URL").
//   * DATA IS VERBATIM: user-authored marker labels, titles and filenames,
//     literal settings and config KEY names shown as keys (`projects_path`),
//     and the routing/category tokens that are data rather than prose.
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
// roster's keys, 2026-10-22. Its face
// answer, ButtonBoxFace and the glyph's shift it carried, went 2026-10-23.)

// ARC'S ENTRY'S LENGTHS (architect 2026-11-19, send Z; DESIGN §3, the kit's
// body_entry; G3 `entry`): the box on the keys' rows, the key's height (28 W
// at the default, 56 device rows on the tablet), its WIDTH kEntryPadsW (18 W)
// PLUS THE ADVANCE OF ITS SPECIMEN — the widest text it holds, in its face
// at the live scale — ROUNDED UP TO A WHOLE W, then through scaled_px; its
// text's PEN kEntryPenW (9 W, materials.h) inside its left edge, its
// BASELINE by the line-box rule in the key's height (+18 W in the 28 W box,
// line_box_baseline). NOISE's specimens below (39 W, 78 device px on the
// tablet). The painter is materials.h's paint_entry, which owns the entry's
// own height and pen (kEntryHeightW, kEntryPenW; the value sub-page's
// entry stands on no key row and takes both).
constexpr double kEntryPadsW = 18.0;
static int entry_w_px(GuiFace face, std::string_view specimen) {
    const double adv_px =
        text_shape::memoized_run(gui_font(face), specimen)->width_px;
    const double factor = gui_scale_factor();
    const double adv_w  = factor > 0.0 ? adv_px / factor : 0.0;
    return scaled_px(std::ceil(kEntryPadsW + adv_w), 1);
}
// A LABEL'S SLOT (send Z's design, built in send AA: the controls rows' time
// labels and the score viewer's page cell, GTK labels, no entry round
// them): its specimen's advance in its face at the live scale, ROUNDED UP TO
// A WHOLE W, then through scaled_px.
static int label_w_px(GuiFace face, std::string_view specimen) {
    const double adv_px =
        text_shape::memoized_run(gui_font(face), specimen)->width_px;
    const double factor = gui_scale_factor();
    const double adv_w  = factor > 0.0 ? adv_px / factor : 0.0;
    return scaled_px(std::ceil(adv_w), 1);
}
static double entry_baseline(const GuiFont& font, const GuiRect& entry) {
    return line_box_baseline(font, static_cast<double>(entry.y),
                             key_height_w());
}
// NOISE's specimens and its box's width (the head above; architect
// 2026-11-19, send AA, RULINGS §8): the case prints one decimal under 10 dB
// of magnitude and none from 10 (format_level_db_face), so the entry is the
// pads plus THE WIDEST ADVANCE OF THE FOUR SHAPES IT CAN TAKE — a one-decimal
// number, a two-digit one at the travel's floor and its neighbor, and -inf:
// at Regular 12 W "-9.5" 20.68 W, "-22" and "-60" 17.34 W, "-inf" 16.67 W,
// so ceil(18 + 20.68) = 39 W, 78 device px on the tablet (the digits are
// tabular: every one-decimal number is "-9.5"'s advance, "0.0" one minus
// short of it). The ceiling is monotone, so the widest specimen's box is
// the widest box.
constexpr std::array<std::string_view, 4> kNoiseEntrySpecimen = {
    "-9.5", "-22", "-60", "-inf"};
static int noise_entry_w_px() {
    int w = 0;
    for (const std::string_view s : kNoiseEntrySpecimen)
        w = std::max(w, entry_w_px(GuiFace::Body, s));
    return w;
}

// The key's drawing (its rule at the definition below).
void paint_key_glyph(cairo_t* cr, const GuiRectF& key, icons::Icon icon,
                     uint32_t tone);
void paint_key_glyph(cairo_t* cr, const GuiRectF& key, icons::Icon icon,
                     const StampTone& ink);
void paint_key_glyph(cairo_t* cr, const GuiRect& key, icons::Icon icon,
                     const StampTone& ink);

// Whether the clip reaches a rect (its rule at the definition below).
static bool clip_meets(cairo_t* cr, const GuiRect& rect);

// A ROSTER GROUP'S PAINT (send Z, 2026-11-19; materials.h's
// paint_linked_group): the members `ids`, already published at their boxes
// from (x, y) (publish_button_face, which stashes each one's enabled and
// selected bits as painted), painted as ONE LINKED GROUP — each member's
// state key_state's answer off its stashed bits and the live press
// (redesign_button_pressed_face, gated on the painted enabled bit: a key can
// go dead under a held press), its LOOK that state with the hover (a resting
// member that is AppState::FaceFades::hovered is Hover; face_look) — EACH
// MEMBER AND EACH SEAM AS PAINTED (architect 2026-11-20, send DD: "i'd like
// to see arc-truthful version instead"; AppState::FaceFades, the one owner):
// the member's FaceMix and the seam's line by face_as_painted /
// seam_as_painted off the looks this run resolved, which begin the fade of a
// member whose look moved (a lit or dark edge, a greying — a modal's among
// them: an editor, a question or the progress page greys the whole standing
// top icon row, and its keys fade, AppState::FaceFades — a feint, a lift)
// and of its seams in the same call, and hand back the picture on screen —
// on the box's own rect, a member this run's clip misses unchanged — then
// each member's drawing on
// its box (paint_key_glyph) in its ink as painted (key_ink_tone: the drawing
// fades with the face; its icon, a discrete property, the state's at once).
// The band's runs and the bottom icon row's groups both paint through it.
void paint_roster_group(cairo_t* cr, AppState& app, const GuiAudio& audio,
                        int x, int y, std::span<const RedesignButton> ids) {
    using FaceFades = AppState::FaceFades;
    const std::size_t n = ids.size();
    std::vector<FaceMix> faces(n);
    std::vector<double>  seam(n, 0.0);
    std::vector<FaceLook> looks(n, FaceLook::Rest);
    FaceFades& fades = app.face_fades;
    for (std::size_t i = 0; i < n; ++i) {
        const int idx = redesign_button_index(ids[i]);
        const AppState::RedesignButtonFace& f =
            app.redesign_buttons[static_cast<std::size_t>(idx)];
        const bool pressed =
            f.enabled && redesign_button_pressed_face(app, ids[i]);
        const FaceFades::Face face{FaceFades::Surface::Roster, idx};
        looks[i] = face_look(key_state(f.selected, pressed, f.enabled),
                             fades.hovered == face);
        const GuiRect box = linked_key_box(x, y, static_cast<int>(i));
        const bool exposed = clip_meets(cr, box);
        faces[i] = fades.face_as_painted(face, box, looks[i], exposed);
        if (i > 0)
            seam[i] = fades.seam_as_painted(face, box, looks[i - 1], looks[i],
                                            exposed);
    }
    paint_linked_group(cr, x, y, faces, seam);
    for (std::size_t i = 0; i < n; ++i)
        paint_key_glyph(cr, linked_key_box(x, y, static_cast<int>(i)),
                        redesign_button_icon(app, audio, ids[i]),
                        key_ink_tone(faces[i]));
}

// THE LEVEL CASE'S PAINTER — NOISE AS ARC'S ENTRY WITH ARC'S ENTRY PROGRESS
// (architect 2026-11-19, send Z; DESIGN §3, the kit's body_entry; the
// ruling of the case itself at RedesignButton::IconNoiseLevel, app_state.h,
// architect 2026-11-05, session 46): the `noise_level` SLIDER at the band's
// third run's tail, a run gap after it, ITS OWN BOX `entry` (noise_entry_w_px
// wide, the key's height): THE ENTRY (materials.h's paint_entry: the canvas
// tone in a one-W `well_border`, its corners 3 W) — NEVER LIT, NEVER PRESSED:
// a slider shows its value (his "no press face on the cases", 2026-11-05) —
// and THE LEVEL ON IT AS ARC'S ENTRY PROGRESS: a 2 W line of the accent
// along the entry's foot inside its border, from the left over t of the
// inner width, cut by the inner corner, t the exact level's travel fraction
// (level_travel_fraction, the square-law taper, app_state.h: 0 dB the whole
// width, -9 dB 0.596 of it, -inf none) — its right end a real x, every
// column it crosses at its coverage, so the line moves with every change of
// the level, however small. AT -inf NO LINE. THE NUMBER: one decimal under
// 10 dB of magnitude, none from 10 — "0.0", "-9.5", "-22", "-inf"
// (format_level_db_face, THE LEVEL GRAMMAR, device_config.h; architect
// 2026-11-19, send AA: "doesn't need decimals once it hits 10"), the case's
// own `noise_level` alone (the bed's fixed attenuation is not printed,
// noise.h's head), in the BODY face (Regular 12 W), CENTERED IN THE ENTRY
// (architect 2026-11-19, send AA, RULINGS §10: "I guess the text should
// probably be centered" — "Agree."; alignment is the app's property in GTK,
// its default left, and the entry is its widest number's width, so a
// left-aligned "-inf" left air on its right): the shaped run's advance
// centered on the entry's inner width at its exact place, no rounding (the
// border is one W each side, so the inner width's center is the box's), on
// the line-box baseline, `text` — at -inf too
// (architect 2026-11-17, send U: "Negative INF on the noise slider should
// not have a disabled color because it's still enabled"). DISABLED
// (`enabled` false — no folder or a failed load, and the roster's blanket
// greys: a load, an overlay, the on-screen keyboard standing, send EE): THE
// NUMBER AT 35 % OVER THE FILL (Arc's insensitive entry, the
// kit's; the mix per channel of the installed words, rounded) and NO LINE.
// The run shaped through the memo (memoized_run: a few dozen spellings).
// THE REPAINT: every change of the case's level damages its box explicitly
// (GuiInputHandler::apply_level_case) — the per-tick comparator watches no
// level — and the enabled edge is the comparator's as every key's.
void paint_level_case(cairo_t* cr, const GuiRect& entry, bool enabled,
                      double level_db) {
    if (entry.w <= 0 || entry.h <= 0) return;
    const double t = std::clamp(level_travel_fraction(level_db), 0.0, 1.0);
    paint_entry(cr, entry, enabled && t > 0.0 ? std::optional<double>(t)
                                              : std::nullopt);
    const GuiFont font = gui_font(GuiFace::Body);
    const text_shape::SharedRun run =
        text_shape::memoized_run(font, format_level_db_face(level_db));
    const GuiThemeTones& tones = installed_theme().tones;
    const uint32_t ink =
        enabled ? tones.text : mix_word(tones.ruler_face, tones.text, 0.35);
    cairo_save(cr);
    cairo_rectangle(cr, entry.x, entry.y, entry.w, entry.h);
    cairo_clip(cr);
    set_palette_source(cr, hex(ink));
    const double pen_x = static_cast<double>(entry.x) +
                         (static_cast<double>(entry.w) - run->width_px) / 2.0;
    text_shape::show_shaped_run(cr, *run, pen_x,
                                entry_baseline(font, entry));
    cairo_restore(cr);
}

// THE RENDER PLAYER'S GLYPHS (architect 2026-11-01, M7, Breeze's
// roster as ruled, 2026-11-02, R12 / R14), by the act each key runs (its
// name the tooltip's, render_player_button_hint): the transport triple
// PREV MediaSkipBackward, PLAY
// MediaPlaybackStart, NEXT MediaSkipForward (the band's START / END's two,
// R14's transport rule: "two arrows with a bar" on every to-the-end step);
// REPEAT MediaRepeatSingle (the row's one lamp); DOWN / UP GoDown / GoUp;
// FOLDER GoParentFolder; LOAD DialogOkApply (the band's LOAD key's, the
// same act); DELETE EditDelete. (The player has no Close key, architect
// 2026-11-11, session 106. The word buttons carry no drawing since
// 2026-11-18, his ruling "the word alone", so modal_word_icon, their
// vocabulary of drawings, retired 2026-11-19, send Z's stage C.) THE PLAY
// KEY'S GLYPH NAMES ITS NEXT PRESS (architect 2026-10-25; the roster's rule
// at redesign_button_icon's stateful overload) BY ITS FACE
// (render_player_play_face, the owner the tooltip reads too): PAUSE
// MediaPlaybackPause, OPEN over a folder row DocumentOpen — its repaint the
// row's one damage owner, the play face's writers damaging the row whole.
// REPEAT is a toggle, one drawing in both states. No glyph for None.
std::optional<icons::Icon> render_player_button_icon(
        AppState::PlayerButtonAct act, PlayerPlayFace face) {
    using Act = AppState::PlayerButtonAct;
    using icons::Icon;
    switch (act) {
        case Act::Home:          return Icon::MediaSkipBackward;
        case Act::PlayPause:
            switch (face) {
                case PlayerPlayFace::Pause:      return Icon::MediaPlaybackPause;
                case PlayerPlayFace::OpenFolder: return Icon::DocumentOpen;
                case PlayerPlayFace::Play:       break;
            }
            return Icon::MediaPlaybackStart;
        case Act::NextTrack:     return Icon::MediaSkipForward;
        case Act::RepeatOne:     return Icon::MediaRepeatSingle;
        case Act::HighlightDown: return Icon::GoDown;
        case Act::HighlightUp:   return Icon::GoUp;
        case Act::Up:            return Icon::GoParentFolder;
        case Act::LoadInPlace:   return Icon::DialogOkApply;
        case Act::Delete:        return Icon::EditDelete;
        case Act::None:          break;
    }
    return std::nullopt;
}

// A KEY'S DRAWING ON ITS BOX (architect 2026-11-01 / 2026-11-02; icons.h's
// THE INK BOX) — THE ONE PLACE the drawing's seat, size and tone are
// decided, every key's (the band's, the bottom icon row's, the controls
// rows' glyph keys, and the on-screen keyboard's glyph keys —
// onscreen_keyboard::cap_icon, architect sessions 95 / 96: "one ink size
// with the program keys"); THE TOOLTIP names the act in words.
// THE SET'S DRAWING CANVAS AT kKeyDrawingBoxW, 16 W (architect 2026-11-19,
// send Z; DESIGN §3: Papirus's 16-px drawings at their native size are the
// design, Breeze taking the same rule), 32 device px on the tablet — ONE
// SIZE FOR EVERY KEY, no longer a share of `key_height` (the 9 / 17 ink of
// 2026-11-04 night retired) — the drawing scaled as its whole canvas into
// that box (icons::ink_in_box: its ink's length and its offset in the box),
// THE BOX CENTERED IN THE KEY'S BOX EXACTLY (+10 W, +6 W in a 36 x 28 W key:
// 20 and 12 device px on the tablet; at another key size half the spare on
// each axis, a real place, the no-grid rule), the drawing rasterized at its
// real place (icons::draw_glyph) in `tone`, the caller's — key_ink of the
// key's state (materials.h), the on-screen keyboard's lit or rest print. The
// drawing is clipped to the key's whole pixels (its hull): a key smaller
// than the box keeps its drawing centered and lets nothing overhang onto the
// ground.
constexpr double kKeyDrawingBoxW = 16.0;
void paint_key_glyph(cairo_t* cr, const GuiRectF& key, icons::Icon icon,
                     uint32_t tone) {
    if (!(key.w > 0.0) || !(key.h > 0.0)) return;
    const int box = scaled_px(kKeyDrawingBoxW, 1);
    const icons::InkInBox fit = icons::ink_in_box(icon, box);
    if (fit.ink_px <= 0) return;
    const double bx = key.x + (key.w - static_cast<double>(box)) * 0.5;
    const double by = key.y + (key.h - static_cast<double>(box)) * 0.5;
    const GuiRect clip = hull(key);
    cairo_save(cr);
    cairo_rectangle(cr, clip.x, clip.y, clip.w, clip.h);
    cairo_clip(cr);
    icons::draw_glyph(cr, icon, bx + fit.left, by + fit.top, fit.ink_px, tone);
    cairo_restore(cr);
}
// THE DRAWING IN ITS INK AS PAINTED (architect 2026-11-20, send DD; THE
// FACE'S FADE, AppState::FaceFades): `ink` the face's mix of its looks' inks
// (key_ink_tone, materials.h). A plain ink is the word road above, byte
// for byte; a mixed one the same seat and size through the mask road
// (icons::draw_glyph_tinted: the raster of the mix's first ink, its
// coverage the mask of the mixed word).
void paint_key_glyph(cairo_t* cr, const GuiRectF& key, icons::Icon icon,
                     const StampTone& ink) {
    const auto word_of = [](GuiColor c) {
        const auto ch = [](double v) {
            return static_cast<uint32_t>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0));
        };
        return (ch(c.r) << 16) | (ch(c.g) << 8) | ch(c.b);
    };
    if (ink.plain()) {
        paint_key_glyph(cr, key, icon, word_of(ink.first()));
        return;
    }
    if (!(key.w > 0.0) || !(key.h > 0.0)) return;
    const int box = scaled_px(kKeyDrawingBoxW, 1);
    const icons::InkInBox fit = icons::ink_in_box(icon, box);
    if (fit.ink_px <= 0) return;
    const double bx = key.x + (key.w - static_cast<double>(box)) * 0.5;
    const double by = key.y + (key.h - static_cast<double>(box)) * 0.5;
    const GuiRect clip = hull(key);
    cairo_save(cr);
    cairo_rectangle(cr, clip.x, clip.y, clip.w, clip.h);
    cairo_clip(cr);
    icons::draw_glyph_tinted(cr, icon, bx + fit.left, by + fit.top,
                             fit.ink_px, word_of(ink.first()),
                             stamp_tone_word(ink));
    cairo_restore(cr);
}
void paint_key_glyph(cairo_t* cr, const GuiRect& key, icons::Icon icon,
                     const StampTone& ink) {
    paint_key_glyph(cr,
                    GuiRectF{static_cast<double>(key.x),
                             static_cast<double>(key.y),
                             static_cast<double>(key.w),
                             static_cast<double>(key.h)},
                    icon, ink);
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
// fully clipped path saved (the icon rows' keys, the bottom icon row's
// left run, under the narrow
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
// drift on the next tick, and its own damage — the drifted key's rect, cut
// to cover what this gate asks, or the key's home strip (main.cpp's walk) —
// is what repaints and republishes: the comparator is the repair mechanism.
// An EMPTY rect refreshes unconditionally (as does one wholly off the
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
// end-of-song teardown, damaging no button) now flips that button's
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
// (2026-10-23): `painted` — the face, cut by whatever clip the row paints
// under (the band's overflow column), since the printed legend under it
// retired (2026-11-18, send Z) — is the rect the clip must cover, because
// the face wears the enabled bit and a refresh over a clip that missed it
// would stamp a bit its pixels never took; `rect`, the face, is what the
// face publishes for the press, the hover and the tooltip.
//
// THE GLYPH IS THE FOURTH TERM (2026-11-02; the printed word's from
// 2026-10-25): `icon` is the drawing the key wears
// (redesign_button_icon's stateful overload — the act its next press runs),
// stashed beside the index so the comparator sees FIT / RECALL and INHERIT
// / TIE, which move no index. THE COVERAGE IS THE UNION OF THE PAINTED
// EXTENT AND THE ONE LAST STASHED (RedesignButtonFace::painted), so a face
// whose extent moved is repainted whole before the stash may say the new
// drawing.
//
// A MEMBER THAT DOES NOT STAND publishes the EMPTY rect and paints nothing
// (the modal yield, the overflow, the `h` view's stand-ins and hide, above;
// the overload below); its stash refreshes unconditionally (the empty rect
// has no pixel to be stale) and its painted extent empties (the walk's own
// damage having erased what stood there).
AppState::RedesignButtonFace& publish_button_face(
    cairo_t* cr, AppState& app,
    const GuiAudio& audio, const GuiPlayback& playback,
    const GuiTargetRender& target_render,
    RedesignButton id, const GuiRect& rect, const GuiRect& painted,
    int seam = 0) {
    AppState::RedesignButtonFace& face =
        app.redesign_buttons[redesign_button_index(id)];
    face.rect = rect;
    face.seam = rect.w > 0 && rect.h > 0 ? seam : 0;
    if (rect.w <= 0 || rect.h <= 0) face.painted = GuiRect{0, 0, 0, 0};
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
        face.icon  = redesign_button_icon(app, audio, id);
        face.painted = painted;
    }
    return face;
}
AppState::RedesignButtonFace& publish_button_face(
    cairo_t* cr, AppState& app,
    const GuiAudio& audio, const GuiPlayback& playback,
    const GuiTargetRender& target_render,
    RedesignButton id, const GuiRect& rect) {
    return publish_button_face(cr, app, audio, playback, target_render, id,
                               rect, rect, 0);
}

// ROW 4 — THE ICON ROW: THE TOOLBAR BAND (architect 2026-10-09, the program
// is Cool Edit; the simple layout 2026-10-22, program_spec.h's head). Its
// metrics — the case and its glyph seat, Arc's toolbar pads and the run gap —
// live in render.h's program block, where the lane table
// and row 8 read them too; its ends are the well's first and last columns
// (render.h's well_columns, 2026-10-23); this row spells none of
// them.
//
// THE VERTICAL STORY (architect 2026-11-18 / 2026-11-19, send Z; render.h's
// band paragraph): the title bar (render.h's title bar block), Arc's pad
// 7 W, the key (band_case_top_offset_px), the pad 7 W; the bottom icon row
// the pad 7 W, its key, the pad 7 W (symmetric, architect 2026-11-20, send
// EE; render.h's ARC'S TOOLBAR PADS).
//
// THE HORIZONTAL WALK (architect 2026-10-22: "push all icons
// together … add a spacing after render, and after flatten"): the roster's
// left members in THREE RUNS on the body tone — Open Project .. Render,
// Zoom In .. Flatten, Play Renders .. Tooltips — EACH RUN ONE LINKED GROUP,
// Arc's linked buttons (send Z, 2026-11-19: the sub-groups inside a run no
// longer parted; materials.h's THE KEY and linked_group_w_px), NOISE's entry
// a run gap after the third, and the run gap of ground between two
// (render.h's program_run_gap_px, the `run_gap` tuning key's W, 6 its
// default, 12 device px on the tablet; redesign_button_opens_icon_run,
// app_state.h, the runs' leaders); the right five — the history opener and
// the two view pairs — ONE GROUP ending 6 W inside the panel's right edge,
// the left walk starting 6 W inside its left edge (icon_row_seats;
// render.h's kEdgeInsetW). No pane, groove, recess
// or line (Cool Edit's panes and grooved recess stood 2026-10-09 to
// 2026-10-22).

// THE PAINTER'S HALF OF THE ICON-ROW ROSTER: each button's id, in painted
// order; the key wears its glyph (redesign_button_icon, 2026-11-02).
// The press claim's chord table (input_pointer.cpp) is the
// other half; both key off the same ids.
//
// WHAT LEADS A BUTTON IS NOT HERE ANY MORE (2026-08-13): the struct carried an
// IconRowLead column — First / Gap / Separator — of which only Separator ever
// meant anything to the walk, First and Gap taking the same branch. The row's
// GROUP BOUNDARIES are redesign_button_opens_icon_group's (app_state.h), the
// unit of the `h` view's stand-in swap, and its RUN BOUNDARIES — the run gap
// of bare ground, the band's one spacing since the simple layout (2026-10-22) —
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
    // stateful glyphs, redesign_button_icon's stateful overload; Render serves BOTH plain
    // render and the iteration sweep by the architect's same-day ruling, the
    // tooltip alone forking).
    //
    // OPEN PROJECT LEADS THEM ALL (architect 2026-10-07), ONE BUTTON — "just
    // to keep those two together", the button and its shift twin, Revert:
    // no drop-down — and THE FIVE ARE THE BAND'S FIRST RUN (the simple
    // layout, 2026-10-22: Open, Save, Undo, Redo and Render one run, the run
    // gap after Render; redesign_button_opens_icon_run). Undo still leads a
    // GROUP with Redo and Render (redesign_button_opens_icon_group), the unit
    // the `h` view's stand-ins replace, which paints nothing.
    //
    // (COPY PHASE RESETS stood between Redo and Render 2026-10-25 to
    // 2026-11-07, a member of Undo's group; it is row 8's marker verbs' tail
    // since, right of DRAG — kMarkerVerbGroup — so the first run is OPEN SAVE
    // UNDO REDO RENDER again, architect 2026-11-07, session 87.)
    {RedesignButton::OpenProject},
    {RedesignButton::Save},
    {RedesignButton::Undo},
    {RedesignButton::Redo},
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
    // the long press two (RedesignButton::IconZoomIn's record), IN and
    // OUT. Two cases onto the walk at the second run's head, no run
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
    // (2026-10-24), the command's pointer home (the
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
    // RESTRICT (architect 2026-10-29): bare `r`, the lamp that keeps the
    // user's play inside the window, behind Follow, its radio partner. One
    // case onto the second run, no run and no separator moving.
    {RedesignButton::IconRestrict},
    // THE TRIM ARM LAMP (architect 2026-11-07, session 87), behind Restrict:
    // bare `w`, TRIM, the lamp that arms the next waveform drag as the trim
    // sweep (RedesignButton::IconTrimArm). One case onto the second run, the
    // band keeping its count (COPY having left the first run for row 8).
    {RedesignButton::IconTrimArm},
    // THE PRESETS KEY (architect 2026-10-28 night; PRESETS since 2026-11-07),
    // the zoom group's TAIL behind the TRIM lamp: bare `t`, the trim presets
    // list's opener and, while it stands, its closer
    // (RedesignButton::IconTrimPresets). No run and no separator moving (the
    // iteration group's stays BPM's).
    {RedesignButton::IconTrimPresets},
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
    // The read-only toggle: its lit face says read-only, as every
    // toggle's does.
    {RedesignButton::IconReadOnly},
    // THE SCORE (architect 2026-10-26), between the padlock and Settings
    // (architect 2026-11-10: the run reads PLAYER LOCK SCORE SETUP TIPS
    // NOISE since the AUDIO case left, 2026-11-11): bare `m`, the score
    // viewer's opener and, while it stands, its closer (RedesignButton::IconScore). One case on the walk, no run
    // moving.
    {RedesignButton::IconScore},
    // SETTINGS (architect 2026-09-29), right after the Score key (architect
    // 2026-11-10) and ahead of
    // the tooltip lamp (architect 2026-10-01: help comes after settings): bare
    // `;`, the settings page (2026-10-28) — the key's pointer spelling, one box and one 2px gap onto the walk
    // and no separator moving.
    {RedesignButton::IconSettings},
    // ENABLE TOOLTIPS (architect 2026-09-29, evening), behind Settings
    // since 2026-10-01 (architect: help
    // comes after settings), the level case behind it: the bare backslash's LAMP. Dark at every open, and while dark no tooltip shows
    // anywhere; lit, the Qt model as ruled. One box and one 2px gap onto the
    // walk, no separator moving.
    {RedesignButton::IconTooltips},
    // THE LEVEL CASE (architect 2026-11-05), the third run's last, in the
    // NOISE lamp's seat (2026-10-28 to 2026-11-05): NOISE, the `noise_level`
    // slider (RedesignButton::IconNoiseLevel, app_state.h; paint_level_case
    // above). The AUDIO case before it, the project voice's slider, retired
    // 2026-11-11 with its key, the run one pitch shorter, no run moving: on
    // the tablet at the defaults the left walk ends short of the left runs'
    // limit (the right run's first face less the run gap, icon_row_seats;
    // THE FIT at paint_icon_row's width math).
    {RedesignButton::IconNoiseLevel},
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
// is the mode's vocabulary or on its allowlist): UNDO'S (Undo, Redo, Render
// — Ctrl+Z, Ctrl+Shift+Z, Ctrl+Alt+R)
// and THE ITERATION GROUP (Ctrl+B, bare `i`, Ctrl+F). No other group is
// refused whole in the view: the zoom group keeps Zoom In, Zoom Out and Zoom
// to Fit (`=`, `-` and bare `0`) and the render-entry group Play Renders (bare `l`, admitted 2026-10-25),
// the tooltip lamp (bare backslash) and the level case (no chord, LIVE
// by the walk's own arm since 2026-11-05; the NOISE lamp's bare `w`
// admitted 2026-10-28 until then).
//
// THE SLOTS: UNDO'S THREE (Undo, Redo and Render; four with Copy Phase
// Resets 2026-10-25 to 2026-11-07) take the view's two acts, Revert and Load
// in Place, so that group stands one case narrower in the view; THE ITERATION
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
// since 2026-10-25, Enable Tooltips, and the level case since
// 2026-11-05 — the NOISE lamp 2026-10-28 until then), their others hidden
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
// then the walk lamp — CUMUL and SESSION, each key's lit face its state
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
// its menu row — reborn as the selectors, keys since the hi-fi skin
// (2026-10-23).
// THE BAND'S OWN METRICS, NONE NEW: the program's case
// (render.h's program block), the four linked behind the history opener in
// the band's one right group, Phase Reset's box ending 6 W inside the
// panel's right edge (icon_row_seats; the two panes with their groove
// 2026-10-14 to 2026-10-22). THE BAND'S FACES, and EACH
// PAIR A RADIO THAT POPS: its lit member is its axis's current value
// (redesign_button_selected reads one axis per button) and a press on it
// SELECTS THE PAIR'S OTHER MEMBER at its lift (architect 2026-10-23 night,
// "building a suction on the other one is the right way to go"; the consumed
// nothing of 2026-10-14 reversed; the key's own resolution, GuiInputHandler::
// on_key) — never greyed for it — the key staying lit under the hold in
// the one accent (Arc's lit look, pressed or not; the radio keys' purple
// retired 2026-11-19, send Z; materials.h's key_state).
//
// THE OVERFLOW RULE (architect 2026-10-01): AT ANY WINDOW WIDTH WHERE THE ROW
// CANNOT HOLD EVERY GROUP, THE VIEW PAIRS WIN — and since 2026-10-06 the
// history opener seated against their left with them (kIconRowHistoryOpener).
// All paint LAST, whole, at their right-anchored places; the runs to
// their left yield one run gap short of the opener: A MEMBER WHOSE BOX NO
// LONGER FITS IS DROPPED WHOLE — it publishes the empty rect and paints
// nothing, no key cut, its group's ring closing round the members that
// stand — so a press, a hover and a tooltip land on exactly the pixels on
// screen (on screen is as painted; paint_icon_row's walks). The widths at
// which the band holds every run, and which member drops at each width
// past them, are THE FIT at paint_icon_row's width math, its one
// statement; the tablet does not reach the rule at its 200 % and the keys'
// defaults (a key_width of 43 W does, with the default run gap).
constexpr IconRowDef kIconRowViewPairs[2][2] = {
    {{RedesignButton::ViewSource},
     {RedesignButton::ViewTarget}},
    {{RedesignButton::ViewWarp},
     {RedesignButton::ViewPhase}},
};

// THE BAND'S RIGHT RUN (architect 2026-10-22, the simple layout): the
// history opener and the two view pairs, five keys a key pitch apart in painted
// order, right-anchored (icon_row_seats), never hidden.
constexpr IconRowDef kIconRowRightRun[] = {
    kIconRowHistoryOpener,
    kIconRowViewPairs[0][0], kIconRowViewPairs[0][1],
    kIconRowViewPairs[1][0], kIconRowViewPairs[1][1]};

// -- THE FLOATING SURFACES: the hover tooltip and the notification cards --
//
// The tooltip is Arc's (send AA's stage D1, 2026-11-19): its type, its box
// and its shadow are its painter's (paint_shift_tooltip, on the floating
// surfaces' rounded box, paint_floating_box), its lengths, its seat, its
// timing and its damage bound render.h's.
// (The damage BOUND on the height and the timing constants live in render.h —
// the tooltip's clock reads them, for the deadlines and for the band beside
// the strip.)

// THE TOOLTIP'S TEXT lives with the roster, not here
// (redesign_button_tooltip, app_state.h, owns both the membership and the text;
// its MODIFIER line is static_asserted against redesign_button_shift_admits,
// so the hint cannot appear where a modified press does nothing).
//
// EVERY BUTTON BUT ROW 1'S HAS ONE (architect 2026-07-31, stated as the ROW's
// property at the table): the one-line form is the whole story for most, and the
// buttons that admit a modifier add the hint line below it (the membership is
// those predicates, app_state.h, never a count restated here) — Render only
// while iteration mode is OFF, where its shift press has a twin to reach.
// (The TEXT and its membership live at redesign_button_tooltip, app_state.h —
// beside the roster, because the pointer side reads the same table.)

// THE BASELINE FOR A LABEL VERTICALLY CENTERED IN A BOX: the baseline that
// centers the face's own CAP BAND in the box EXACTLY (architect 2026-11-15,
// the no-grid arc: no half is rounded, the baseline a real y and the text
// painted at it — text_raster.h). What the eye centers on a key is the
// capital band, so the solver centers exactly that and lets the descender
// band fall where the face puts it:
//
//     baseline = box_y + (box_h + cap_height) / 2
//
// so the air above the cap band and under the baseline is the same real
// length. ITS ONE CALLER IS THE ON-SCREEN KEYBOARD'S CAPS
// (paint_onscreen_keyboard), a cap centered on its key's face.
//
// THE CAP BAND IS THE FACE'S OWN (gui_font.h, gui_font_cap_px): the em
// times the file's own "H" ink per em (Liberation Sans 1409 / 2048 units),
// an unrounded double — 16.51 device rows at the tablet's 200 % for the
// Keys face's default 12 W (a 24-device-px em). On the tablet at osk_key_h
// 35 (a 70-row key) a cap's baseline stands (70 + 16.51) / 2 = 43.26 rows
// under the key's top.
//
// EVERY OTHER TEXT TAKES ANOTHER SEAT. THE LINE-BOX RULE (render.h's
// line_box_baseline, GTK's seat; send Z, 2026-11-19): the face's line box
// centered in a box of whole W, the baseline its ascent under the box's
// top — the bottom icon row's left run, the render player's and the score
// viewer's time labels and page cell, the lists' rows and headings, the
// pages' lines, the word buttons' words and the value sub-page's entry, the
// status bar, the cards, the tooltip, the
// NOISE number, the ruler's digits (render.h's ruler_baseline_px) and the
// cues' labels (cue_baseline_px, the flag editor's caret and selection band
// the label face's line box under it, cue_line_box_top_px). THE TITLE BAR'S
// TEXT stands on a stated baseline (kTitleBaselineW; paint_title_bar). (This
// solver is declared in render.h for its readers there.)
} // namespace

double redesign_baseline(const GuiFont& font, double box_y,
                         double box_h) {
    return box_y + (box_h + gui_font_cap_px(font)) * 0.5;
}
double line_box_baseline(const GuiFont& font, double box_y, int box_w) {
    // The rule at the declaration (render.h): the line box's top floor((box
    // − line) / 2) W down, the baseline the ascent under it, one whole-W
    // offset through scaled_px.
    const GuiLineBoxW lb = gui_font_line_box_w(font);
    const int d     = box_w - lb.line();
    const int top_w = d >= 0 ? d / 2 : -((1 - d) / 2);   // floor(d / 2)
    return box_y + static_cast<double>(scaled_px(top_w + lb.ascent));
}
// (THE CAPTION'S PAINTER — paint_caption_row, the window's own title bar
// at the top lane 0 from 2026-10-05: the quaver, "<piece> - Warptempo" and
// the three procedural glyphs, the hi-fi skin's flat lane 2026-10-23 — is
// REMOVED 2026-10-24 with the lane, architect: "release it to the window
// manager as we had in the beginning of this project"; the tablet's window
// has none. Git history. Since 2026-11-19, send Z, the band's first rows
// are the app's TITLE BAR, paint_title_bar — a bar of the program's, no
// window chrome.)

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
// (history_walk_line below) — one token, one spelling. (The status bar's
// fields part by Thunar's wider "  |  ", kStatusFieldSeparator.)
static constexpr const char* kSegmentSeparator = " | ";

// -- history_walk_line ----------------------------------------------------
//
// THE `h` HISTORY MODE'S ONE LINE, composed for THE STATUS BAR'S FIRST FIELD
// (status_bar_text; send Z, 2026-11-19). While the mode stands this line is
// what that field is for.
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
// line, a plain run, has no strike to carry the side. THE LINE STANDS AT
// THE STATUS BAR'S LEFT, its first field (paint_status_bar the one painter
// of it), under the bottom icon row.
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
    // walks, because what the view's Save key does depends on it
    // whichever walk is showing. It stands ahead of the scale because the
    // status bar clips its line at the panel's right edge (paint_status_bar):
    // where the line is wider than the bar the cut falls on the scale, which
    // the diff lane also shows, and never on the word Ctrl+S forks on. A visit with no clone (the local fallback)
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
        app.history_mode.displayed_delta();
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

// THE STATUS BAR'S STRING — ITS ONE COMPOSER (architect 2026-11-19, send Z;
// DESIGN §5, his rulings of that day; render.h's status bar block): STANDING
// STATE ONLY, NEVER AN EVENT (events stay cards; "No, I decline both of
// those" — no card retires into it), EACH FIELD SHOWN ONLY WHILE IT HAS
// SOMETHING TO SAY, in this order, joined by kStatusFieldSeparator, "  |  "
// (Thunar's, two spaces each side):
//   1. THE PROCESS LINE ("a must-have"), the state line of notifications.h's
//      head WORD FOR WORD, moved here from the bottom icon row: in the `h`
//      view the walk line (history_walk_line, which wins: a render
//      dispatched before the visit runs on through it), else
//      AppState::queue_progress_text — a render's "Rendering...", a sweep
//      cell's "Rendering N of M <label>...", a preview's "Updating...".
//   2. THE SELECTION COUNT, only while one or more markers are selected
//      ("only when there's ... one or more selected, it would say the
//      number"), outside the `h` view: the size of
//      AppState::selected_markers on the column it lives on
//      (AppState::active_markers_view — the selection is cleared on a column
//      switch, so it never mixes), "1 warp marker selected", "5 warp markers
//      selected", "1 phase reset selected", "3 phase resets selected" (warp
//      is never the unmarked default). Nothing else rides it.
//   3. GITHUB, IN THE `h` VIEW ONLY ("Keep it in the history only"): the
//      remote's standing state the view already prints — the walk line's
//      own "GitHub: <word>" segment (github_status_word), so field 1 carries
//      it and no third field is composed (the history view has no other
//      word for the remote; nothing here opens the network).
// UNDER AN OVERLAY OR A MODAL (modal_box_stands — the folder overlay's
// contents, a dialog editor, the score viewer, a question's page — or an
// open text editor, the flag editor's among them) the bar keeps fields 1
// and 3 and DROPS FIELD 2 (his "agree").
// WHILE A BATCH'S PROGRESS STANDS THE BAR IS BLANK (architect 2026-11-20,
// send EE, to "blank the status bar while the progress page stands":
// "agree"): the string is empty for as long as AppState::batch_progress is
// active — the progress page says what the batch's process line would
// ("Rendering 3 of 11 ..."), so the bar under it does not say it twice —
// WHATEVER STANDS OVER THE PROGRESS PAGE, a question raised over it
// included (its page shows the question; the batch's line stays off the bar
// until the batch's terminal closes the progress). The lane's line and fill
// paint as always (paint_status_bar). Every field goes, the `h` view's walk
// line with the rest, should the view stand under the batch.
// The tick holds this string against the one last painted
// (refresh_status_bar, main.cpp) and damages the bar's rect alone on a
// change — the progress page's open and close among them; the reference is
// non-const for the walk line's lazily cached delta.
static constexpr const char* kStatusFieldSeparator = "  |  ";
std::string status_bar_text(AppState& app) {
    std::string out;
    if (app.batch_progress.active) return out;
    const auto field = [&](const std::string& f) {
        if (f.empty()) return;
        if (!out.empty()) out += kStatusFieldSeparator;
        out += f;
    };
    const bool history = app.history_mode.active;
    field(history ? history_walk_line(app) : app.queue_progress_text);
    const bool covered = modal_box_stands(app) ||
                         app.text_editor_session() != 0;
    if (!history && !covered && !app.selected_markers.empty()) {
        const std::size_t n = app.selected_markers.size();
        const bool phase = app.active_markers_view == 'P';
        std::string f = std::to_string(n);
        f += phase ? (n == 1 ? " phase reset selected"
                             : " phase resets selected")
                   : (n == 1 ? " warp marker selected"
                             : " warp markers selected");
        field(f);
    }
    return out;
}

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
// (app.history_mode.active) for the painter and the fingerprint.
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

// THE BAND'S SEATS (2026-10-21: lifted out of paint_icon_row; the simple
// layout's runs 2026-10-22; ARC'S LINKED GROUPS since 2026-11-19, send Z,
// DESIGN §3, the kit's Layout): THE KEY the title bar and the pad
// under the band's top (band_case_top_offset_px). THE RIGHT RUN IS RESOLVED
// FIRST, because it decides where every run to its left may stand (the
// overflow rule at kIconRowViewPairs): ONE LINKED GROUP whose last box ends
// kEdgeInsetW (6 W) INSIDE THE PANEL'S RIGHT EDGE (render.h's THE EDGE RULE
// at well_columns), and THE LEFT RUNS' LIMIT stands one run gap left of its
// first box. THE LEFT WALK's first box starts 6 W INSIDE THE PANEL'S LEFT
// EDGE, EACH RUN ONE LINKED GROUP (linked_group_w_px, materials.h: its
// members sharing one seam column each), the next group — or NOISE's entry
// at the third run's tail — a run gap after it.
struct IconRowSeats {
    int btn_y = 0;
    int gap = 0;
    int left_x = 0;      // the left walk's first box
    int right_x = 0;     // the right run's first box (the history opener)
    int left_limit = 0;  // the left runs' bound, a run gap short of it
};
static IconRowSeats icon_row_seats(const AppState& app, const GuiRect& lane) {
    const WellColumns well = well_columns(app);
    IconRowSeats s;
    s.gap   = program_run_gap_px();
    s.btn_y = lane.y + band_case_top_offset_px();
    s.left_x = well.x + edge_inset_px();
    s.right_x = well.end - edge_inset_px() -
                linked_group_w_px(static_cast<int>(std::size(kIconRowRightRun)));
    s.left_limit = s.right_x - s.gap;
    return s;
}

// THE TITLE BAR'S LAYOUT (render.h's title bar block, every rule there) —
// ONE ROAD for the painter and the tick's damage (logo_legend_extent), so
// the two cannot disagree: the bar is the band's first title_bar_h_px rows
// (28 W, 56 device px on the tablet); EVERY TEXT ON ONE BASELINE, +18 W
// (kTitleBaselineW), in the Bold 12 W; THE CAP BAND the alphabet's painted
// ink rows above it (kLogoLegendInkSet, gui_font_ink_band), on which the
// battery and the lamps stand; THE RIGHT GROUP ONE TEXT RUN (architect
// 2026-11-19, send AA), laid right to left: the run "  |  <clock>" (the
// status bar's own separator, kStatusFieldSeparator, then the clock) with
// its LAST INK kTitleRightInsetW inside the panel's right edge, the
// battery's cell ending on that run's pen, the run "<percent> " (the
// percent and a space) ending on the cell's left edge, the clip lamp
// kLogoLampGapW left of the percent's first ink (the cell's left edge with
// no percent), the limiter lamp one key gap left of the clip lamp; the
// extent the bar's rows from the first ink column (the limiter lamp's,
// always the leftmost) through the panel's last column.
namespace {
// THE TITLE BAR'S CAP BAND: the alphabet's ink rows above the baseline, in
// the bar's face as painted (gui_font_ink_band) — its rows as they are.
int title_cap_rows() {
    return gui_font_ink_band(gui_font(GuiFace::Bold), kLogoLegendInkSet).above;
}
// THE BATTERY'S INK LENGTH (render.h's THE BATTERY): the cap band's height
// for Papirus's upright drawings (their larger side their height); for
// Breeze's horizontal outline, whose larger side is its width, the length
// that makes its HEIGHT the cap band.
int logo_glyph_ink_px() {
    const int cap = title_cap_rows();
    if (icons::selected_set() == GuiIconSet::Papirus) return cap;
    const icons::InkSize probe =
        icons::glyph_ink_size(icons::Icon::Battery000, 64);
    if (probe.h <= 0) return cap;
    return static_cast<int>(std::lround(static_cast<double>(cap) * 64.0 /
                                        static_cast<double>(probe.h) *
                                        static_cast<double>(std::max(
                                            probe.w, probe.h)) /
                                        64.0));
}
// THE GLYPH CELL: the two battery drawings' common ink box at the one
// length, the larger of the two on each axis.
icons::InkSize logo_glyph_cell() {
    const int glyph_ink = logo_glyph_ink_px();
    const icons::InkSize on =
        icons::glyph_ink_size(icons::Icon::Battery000Charging, glyph_ink);
    const icons::InkSize off =
        icons::glyph_ink_size(icons::Icon::Battery000, glyph_ink);
    return icons::InkSize{std::max(on.w, off.w), std::max(on.h, off.h)};
}
// EVERY PLACE HERE IS REAL (architect 2026-11-15, the no-grid arc): the
// runs' pens and their baseline, and the cell and the two lamps as exact
// rects off the runs' REAL ink edges and advances — painted through the
// exact fill and the exact icon road; `extent` alone is whole pixels, the
// damage (the hull of everything the group inks).
struct LogoLegendLayout {
    GuiRect lane{0, 0, 0, 0};     // the title bar
    // `clock` the run "  |  <clock>", `percent` the run "<percent> " (null
    // when the percent is empty).
    text_shape::SharedRun clock, percent;
    double clock_x = 0.0, percent_x = 0.0;
    double baseline = 0.0;        // the bar's one baseline
    double cap_top  = 0.0;        // the cap band's top row
    GuiRectF cell{};
    GuiRectF limiter_lamp{};      // THE TWO LAMPS (render.h)
    GuiRectF clip_lamp{};
    GuiRect extent{0, 0, 0, 0};
};
GuiRect title_bar_rect(const AppState& app) {
    const GuiRect band = top_icon_row_area(app);
    return GuiRect{band.x, band.y, band.w, std::min(band.h, title_bar_h_px())};
}
double title_baseline(const GuiRect& bar) {
    return static_cast<double>(bar.y + scaled_px(kTitleBaselineW));
}
LogoLegendLayout logo_legend_layout(const AppState& app,
                                    const GuiLogoLegend& legend) {
    LogoLegendLayout l;
    l.lane = title_bar_rect(app);
    const WellColumns well = well_columns(app);
    const GuiFont bold = gui_font(GuiFace::Bold);
    const int cap = title_cap_rows();
    l.baseline = title_baseline(l.lane);
    l.cap_top  = l.baseline - static_cast<double>(cap);
    // THE CLOCK'S RUN, THE TEXT PIPE BEFORE IT (render.h's title bar
    // block): "  |  <clock>", its last ink's real edge kTitleRightInsetW
    // inside the panel's right edge (nothing floored).
    l.clock = text_shape::memoized_run(
        bold, std::string(kStatusFieldSeparator) + legend.clock);
    l.clock_x = static_cast<double>(well.end) -
                static_cast<double>(scaled_px(kTitleRightInsetW)) -
                text_shape::ink_right_px(*l.clock);
    // THE CELL: its right edge on the clock run's pen (the run's leading
    // spaces are the air between the battery and the pipe); ITS TOP THE CAP
    // BAND'S TOP.
    const icons::InkSize cell = logo_glyph_cell();
    l.cell = GuiRectF{l.clock_x - static_cast<double>(cell.w), l.cap_top,
                      static_cast<double>(cell.w),
                      static_cast<double>(cell.h)};
    // THE PERCENT'S RUN, "<percent> ": its advance ends on the cell's left
    // edge (the space the air between the digits and the battery).
    double percent_first_ink = l.cell.x;
    if (!legend.percent.empty()) {
        l.percent = text_shape::memoized_run(bold, legend.percent + " ");
        l.percent_x = l.cell.x - l.percent->width_px;
        percent_first_ink = l.percent_x + text_shape::ink_left_px(*l.percent);
    }
    // THE TWO LAMPS (render.h's THE TWO LAMPS): the clip lamp kLogoLampGapW
    // of ground left of the percent's first ink's real edge (the cell's left
    // edge with no percent run), the limiter lamp one key gap left of it,
    // both kLogoLampW square with their top ON THE CAP BAND'S TOP.
    const double lamp   = static_cast<double>(scaled_px(kLogoLampW, 1));
    const double clip_x = percent_first_ink -
                          static_cast<double>(scaled_px(kLogoLampGapW)) - lamp;
    l.clip_lamp    = GuiRectF{clip_x, l.cap_top, lamp, lamp};
    l.limiter_lamp = GuiRectF{
        clip_x - static_cast<double>(key_gap_px()) - lamp, l.cap_top, lamp,
        lamp};
    // THE EXTENT, WHOLE PIXELS: the columns from the first ink — the limiter
    // lamp's left edge's column, the leftmost thing by construction —
    // through the panel's last column, the bar's rows, no clamp: every ink
    // stands inside the bar by construction.
    const int first = static_cast<int>(std::floor(l.limiter_lamp.x));
    l.extent = GuiRect{first, l.lane.y, std::max(0, well.end - first),
                       l.lane.h};
    return l;
}

} // namespace

GuiRect logo_legend_extent(const AppState& app, const GuiLogoLegend& legend) {
    return logo_legend_layout(app, legend).extent;
}

LogoLampRects logo_lamp_rects(const AppState& app, const GuiLogoLegend& legend) {
    const LogoLegendLayout l = logo_legend_layout(app, legend);
    return LogoLampRects{l.limiter_lamp, l.clip_lamp};
}

void GuiPaintHandler::paint_title_bar(cairo_t* cr, const GuiRect& band) {
    // THE TITLE BAR (render.h's title bar block, every length scaled_px):
    // its own flat fill in the title tone `ground` (Live's SurfaceArea,
    // arc_dark 2F343F: "the darkest color would only apply to the top line,
    // the title bar", architect 2026-11-18, send Z), then the note, the
    // brand and the right group in `title_text`. NO SHADOW (the flat look).
    const GuiRect lane{band.x, band.y, band.w,
                       std::min(band.h, title_bar_h_px())};
    if (lane.w <= 0 || lane.h <= 0) return;
    const WellColumns well = well_columns(app);
    const GuiPalette& p = palette();
    const GuiColor ink = p.title_text;
    const uint32_t ink_word = installed_theme().tones.title_text;
    cairo_save(cr);
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        paint_cell_rect(cr, lane, p.ground);
        cairo_restore(cr);
    }
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    const double baseline = title_baseline(lane);

    // THE NOTE (MusicNote16th on the ink-box road — the BPM key's drawing
    // too, a deliberate share, architect 2026-11-02, R14 (4)) IN ITS 12 W
    // BOX (architect 2026-11-19, send Z): ITS INK'S LEFT EDGE
    // kTitleNoteInsetW inside the panel (architect 2026-11-19, send AA), its
    // ink centered on the bar's rows exactly.
    const icons::InkInBox note = icons::ink_in_box(
        icons::Icon::MusicNote16th, scaled_px(kTitleNoteBoxW, 1));
    const icons::InkSize note_ink =
        icons::glyph_ink_size(icons::Icon::MusicNote16th, note.ink_px);
    const double note_x =
        static_cast<double>(well.x + scaled_px(kTitleNoteInsetW));
    icons::draw_glyph(cr, icons::Icon::MusicNote16th, note_x,
                      static_cast<double>(lane.y) +
                          static_cast<double>(lane.h - note_ink.h) / 2.0,
                      note.ink_px, ink_word);

    // THE BRAND, a constant: ITS PEN kTitleNoteGapW AFTER THE NOTE'S INK'S
    // RIGHT EDGE (architect 2026-11-19, send AA), the ink box's real edge.
    set_palette_source(cr, ink);
    {
        const GuiFont bold = gui_font(GuiFace::Bold);
        const text_shape::SharedRun brand =
            text_shape::memoized_run(bold, kLogoBrand);
        const double x = note_x + static_cast<double>(note_ink.w) +
                         static_cast<double>(scaled_px(kTitleNoteGapW));
        text_shape::show_shaped_run(cr, *brand, x, baseline);
    }

    // THE RIGHT GROUP (logo_legend_layout): the run "  |  <clock>", the
    // battery in its cell, the run "<percent> ", the two lamps — published
    // as painted for the tick's damage.
    const LogoLegendLayout l = logo_legend_layout(app, app.logo_legend);
    app.logo_legend_rect = l.extent;
    text_shape::show_shaped_run(cr, *l.clock, l.clock_x, l.baseline);
    if (l.percent)
        text_shape::show_shaped_run(cr, *l.percent, l.percent_x, l.baseline);
    // THE TWO LAMPS, ON THEIR REAL EDGES (architect 2026-11-15, the no-grid
    // arc): exact rects off the percent's real ink edge, filled at exact
    // coverage over the bar's fill — each lamp its face at rest (`key_face`) or lit (the
    // limiter `limiter_lamp`, Alert, Arc's warning orange — its own tone
    // since send AA's stage D1, the cards having left Alert for notifyd's
    // fills; the clip `clip_lamp`); the clip lamp, while
    // struck, THE WHOLE SQUARE in `key_dim_ink`, WINNING OVER ITS LIT BIT
    // (architect 2026-11-05 night, session 52) — one three-way pick, struck,
    // else lit, else rest — from the three bits the tick writes
    // (refresh_logo_lamps, main.cpp; render.h's THE TWO LAMPS).
    paint_ink_rect(cr, l.limiter_lamp,
                   app.limiter_lamp_lit ? p.limiter_lamp : p.key_face);
    paint_ink_rect(cr, l.clip_lamp,
                   app.clip_lamp_struck ? p.key_dim_ink
                   : app.clip_lamp_lit  ? p.clip_lamp
                                        : p.key_face);

    // THE BATTERY, the set's drawing by the plug state (GuiLogoGlyph's
    // rule), its ink box centered in the cell exactly, at the cell's real
    // place, in the title text's tone.
    {
        const icons::Icon glyph =
            app.logo_legend.glyph == GuiLogoGlyph::Charging
                ? icons::Icon::Battery000Charging
                : icons::Icon::Battery000;
        const int glyph_ink = logo_glyph_ink_px();
        const icons::InkSize s = icons::glyph_ink_size(glyph, glyph_ink);
        icons::draw_glyph(
            cr, glyph,
            l.cell.x + (l.cell.w - static_cast<double>(s.w)) / 2.0,
            l.cell.y + (l.cell.h - static_cast<double>(s.h)) / 2.0, glyph_ink,
            ink_word);
    }

    cairo_restore(cr);
}

void GuiPaintHandler::paint_icon_row(cairo_t* cr) {
    // THE ICON ROW (top lane 0, at the window's top; row 4 of the
    // redesign; the window's width since 2026-10-21): THE TOOLBAR BAND (the
    // roster block above), the simple layout since 2026-10-22 — the title
    // bar, Arc's pad, the cases, the pad on the body tone (send Z,
    // 2026-11-19; 7 W each side, send EE), the left members in
    // THREE RUNS and the right five in ONE — TWENTY-SEVEN members outside the
    // `h` view and EIGHTEEN inside it (2026-10-29, the Restrict lamp behind
    // Follow, hidden in the view), RE-COUNTED off the roster enum, the
    // painter's tables and the run leaders rather than adjusted: OPEN
    // PROJECT at the row's head (architect 2026-10-07, its Revert twin with
    // it) with SAVE, UNDO, REDO, COPY (2026-10-25) and RENDER behind it in
    // the first run; ZOOM IN, ZOOM OUT, ZOOM TO FIT, THE WAVEFORM
    // MAGNIFICATION LAMP, FOLLOW, RESTRICT and TRIM PRESETS (the viewport
    // class), then THE ITERATION GROUP (the BPM opener, grid iteration mode
    // and FLATTEN) in the second; the RENDER-ENTRY group (Listen, the
    // READ-ONLY toggle, the SCORE key — between the padlock and Settings,
    // architect 2026-11-10 — SETTINGS, ENABLE TOOLTIPS and since 2026-11-05
    // the level case, NOISE (AUDIO beside it until 2026-11-11),
    // in the NOISE lamp's seat of 2026-10-28) the third — then,
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
    // (history_mode_hides_button, input_pointer.cpp) — Follow, Restrict and
    // Trim Presets behind Zoom
    // In, Zoom Out, Zoom to Fit and the magnification lamp (which the view
    // admits since
    // 2026-10-12, architect: he wants to magnify while walking history), and
    // the padlock, the Score key (2026-10-26) and Settings between Play
    // Renders (admitted 2026-10-25) and Enable Tooltips, the level case
    // behind it standing — its
    // neighbors
    // closing up and its run standing
    // while any member does. What the view refuses only BY STATE (Save with
    // nothing to commit, Revert with no subject, Load in Place over an empty
    // walk) and what any other mode refuses wears the DEAD FACE, and a window
    // too narrow for the row drops members whole otherwise, under the view pairs'
    // overflow rule — a width, never a state.
    //
    // THE WIDTH MATH, AT THE DEFAULTS — the box the `key_width` tuning key's
    // 36 W, a group's keys sharing one seam column each, the run gap the
    // `run_gap` tuning key's 6 W (send Z, 2026-11-19: Arc's icon button,
    // linked buttons and toolbar groups; the ends 6 W inside the panel,
    // render.h's kEdgeInsetW; key_height moves none of it): in the tablet's
    // device px at 200 % (one W = 2), each group of n keys 72 + 70 (n − 1)
    // (linked_group_w_px), 12 of ground (the run gap) between two groups;
    // the right group 352, its last box ending 12 px inside the panel's
    // right edge (columns 1940..2291). Outside the `h` view the LEFT WALK is
    // three groups of 5, 11 and 5 and NOISE's entry (39 W, 78 px since
    // 2026-11-19, send AA: noise_entry_w_px),
    //   352 + 12 + 772 + 12 + 352 + 12 + 78 = 1590,
    // from column 12: OPEN .. RENDER 12..363, IN .. FLATTEN 376..1147,
    // PLAYER .. TIPS 1160..1511, NOISE 1524..1601 — the kit's seats
    // (tmp/sendZ/mocks/build_log_mock4.txt) — 338 px of ground before the
    // history opener; inside the view three groups of 4, 6 and 2 (Open, Save,
    // Revert, Load in Place | Zoom In, Zoom Out, Zoom to Fit, the lamp,
    // Cumulative, Session | Play Renders, Enable Tooltips) and NOISE,
    //   282 + 12 + 422 + 12 + 142 + 12 + 78 = 960.
    // A roster move restates these numbers.
    // THE FIT, the one statement every other site points to: outside the
    // view the left walk must end a run gap short of the right group — on
    // the tablet at 200 % 52 · key_width + 8 · run_gap device px against 2246
    // — so at run_gap 6 it holds whole up to key_width 42 W (2232) and from
    // 43 W the overflow rule (kIconRowViewPairs) answers: every member whose
    // box would end past the left runs' limit (the right group's first box
    // less the run gap, 2304 − 12 − linked_group_w_px(5) − 12) publishes the
    // empty rect and paints nothing — no key is cut, so every ring closes
    // whole — the right group whole, and every seat where it stood, the
    // members after a dropped one keeping theirs. ON THE TABLET AT run_gap
    // 6, outside the view, what stands at each key_width (the third group
    // seated at 12 + linked_group_w_px(5) + linked_group_w_px(11) + 24):
    //   36 .. 42 W  every member;
    //   43, 44 W    NOISE dropped (its entry ends at 1896 / 1938 against
    //               the limit 1858 / 1848);
    //   45 W        TIPS too (its box ends at 1890 against 1838);
    //   46, 47 W    SETUP too (1842 against 1828 at 46 W);
    //   48, 49 W    SCORE too;
    //   50 W        LOCK too, PLAYER alone left of its group (key_width's
    //               ceiling).
    // Inside the view (groups of 4, 6 and 2 and NOISE) nothing drops up to
    // 50 W. FROM 46 W SETUP HAS NO BOX, a fact of the rule: committed from
    // the settings page, the page stands and its rows and Close still work,
    // but once it is closed nothing on glass opens it again (bare `;` has no
    // other producer, the on-screen keyboard standing in the editors alone),
    // so the `key_width` line is then reachable only from the device's
    // config file.
    // No assert stands on the fit: the empty publication is the whole answer
    // at every width.
    //
    // NO FOCUS SWAP HERE: the ground has one value focused and unfocused
    // (render.h's palette says so), and so has the menu row's.
    //
    // THE KEYS ARE ARC'S BUTTONS (paint_roster_group, above; materials.h's
    // key_state): at rest, LIT — the accent — under a standing press or when
    // CHECKED, the live fact its chord flips (redesign_button_selected), one
    // look (Arc's `:active` and `:checked`; the radio keys' purple retired
    // 2026-11-19, send Z), and AT REST UNDER THE HOVERING PEN Arc's hover
    // face (send CC, 2026-11-20; paint_roster_group's hover span, the rule
    // AppState::FaceFades's); THE LEVEL CASE its own painter
    // (paint_level_case: Arc's entry, the level its progress line, never lit,
    // never pressed, never hovered). The pointer walk finds the tooltip's
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

    // THE TOP ICON ROW IS THE BODY TONE `body_face` (Arc's $bg_color, arc_dark
    // 383C4A; architect 2026-11-18, send Z: the title tone "would only apply
    // to the top line, the title bar") under every group, between them, at
    // a group's rounded corners and in its two pads, NO LINE between it and
    // the title bar (a tone change only) — laid first under every clip, so
    // the keys' rings and the entry stamp their exact coverage over a fresh
    // ground. The title bar its own fill (paint_title_bar); the keys Arc's
    // linked buttons (paint_roster_group) with their drawings on them
    // (paint_key_glyph).
    cairo_save(cr);
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        paint_cell_rect(cr, lane, palette().body_face);
        cairo_restore(cr);
    }
    paint_title_bar(cr, lane);

    // THE KEYS ARE ARC'S LINKED BUTTONS (architect 2026-11-18 / 2026-11-19,
    // send Z; DESIGN §3; materials.h's THE KEY and paint_linked_group): EACH
    // RUN ONE LINKED GROUP — its standing members' boxes sharing one seam
    // column each inside one bordered ring with rounded outer corners — THE
    // BAND'S SEATS the one layout (icon_row_seats, above, where the right
    // run's place and the left runs' limit are stated). THE PUBLISHED RECT
    // IS THE KEY'S BOX, border included ("the border is clickable"), and ITS
    // SEAM — the column it shares with the key on its left, which belongs to
    // that key for every press and hover (point_on_key's linked form,
    // materials.h) — rides beside it; the box is also the painted extent the
    // publication's coverage test and the repaint read, and the damage of a
    // drift or a press (every pixel a member's state moves lies inside its
    // own box, its group's corners included). NOISE IS ARC'S ENTRY a run gap
    // after the third run's group (paint_level_case). No printed legend
    // under any of them (retired 2026-11-18, send Z); the tooltip names the
    // act.
    const IconRowSeats seats = icon_row_seats(app, lane);
    const int btn_y      = seats.btn_y;
    const int left_limit = seats.left_limit;

    // THE DEAD LOOK (architect 2026-10-02, the AB set; the flat look
    // 2026-11-01, M6; Arc's states since 2026-11-19, send Z): a dead key
    // wears Arc's disabled button (materials.h's KeyState::Disabled: the dim
    // face, the dim border on its stretch of the ring, the dim drawing) — AND
    // A LIT-BUT-INERT KEY (the value drag lamp on a locked tab, the chosen
    // view pair under grid iterations, any lit key under an overlay) keeps
    // its lit state, Arc's button:disabled:checked (`lit_dim_fill` under
    // `text_on_lit_dim`; architect 2026-11-11, session 106, "so both read as
    // a unit"; 2026-11-19, "let's do arcs numbers"): the mode cannot change
    // that state, so hiding it would be a lie. UNDER AN OVERLAY THE LIT
    // STATE IS KEPT TOO (architect 2026-11-11, session 107;
    // redesign_button_selected's head). WHICH KEYS WEAR IT — the two modes
    // (the `h` view's state refusals, the read-only lock's set) and the
    // toolbar migrants' own arms — is redesign_button_enabled's (app_state.h)
    // and history_mode_disables_button's (input_pointer.cpp) alone; this
    // painter decides none of it. THE PRESS (Arc's `:active`, the lit look)
    // is gated on the painted enabled bit rather than trusted: the claim
    // never records a press on a disabled button, but a button can go dead
    // UNDER a held press with no pointer event to refresh it
    // (paint_roster_group).
    //
    // ONE GROUP'S PUBLICATION AND PAINT, shared by both walks: each of the
    // `n_whole` members publishes its box (linked_key_box) and its seam,
    // every member past them the EMPTY rect (the overflow rule below: a
    // member past the left runs' limit does not stand, so the ring closes
    // whole after the last member that fits), and the group paints when the
    // clip reaches it (clip_meets on the group's whole rect).
    const auto paint_group = [&](const std::vector<RedesignButton>& ids,
                                 int gx, int n_whole) {
        std::vector<RedesignButton> standing;
        for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
            const RedesignButton id = ids[static_cast<std::size_t>(i)];
            if (i >= n_whole) {
                (void)publish_button_face(cr, app, audio, playback,
                                          target_render, id, GuiRect{});
                continue;
            }
            const GuiRect box = linked_key_box(gx, btn_y, i);
            (void)publish_button_face(cr, app, audio, playback, target_render,
                                      id, box, box, linked_key_seam_px(i));
            standing.push_back(id);
        }
        if (standing.empty()) return;
        const GuiRect group{gx, btn_y,
                            linked_group_w_px(static_cast<int>(standing.size())),
                            icon_case_h_px()};
        if (!clip_meets(cr, group)) return;
        paint_roster_group(cr, app, audio, gx, btn_y, standing);
    };

    // THE LEFT RUNS, IN TWO PASSES: the walk (icon_row_walk, above) lays
    // out every standing member into its run first, and the runs are then
    // seated and painted, each run's keys ONE GROUP and THE LEVEL CASE,
    // where a run holds it (the third), ITS OWN ENTRY A RUN GAP AFTER THAT
    // GROUP. THE OVERFLOW RULE (kIconRowViewPairs; THE FIT at the width
    // math above): a group keeps the members whose boxes end at or before
    // the left runs' limit — whole at every width the band fits (the
    // tablet's included) — and every member past it, NOISE's entry
    // included, publishes the EMPTY rect and paints nothing, which contains
    // no point and refreshes its bits unconditionally (publish_button_face):
    // NO KEY IS CUT, so every group's ring closes whole round the members
    // that stand. The same clip as ever bounds the paint at the limit.
    // A MEMBER THAT DOES NOT STAND in this state (the `h` view's hide and its
    // stand-in swap) is not in its run at all: its neighbors close up into
    // one ring, and it publishes the empty rect too, so neither a press nor
    // the drift comparator (main.cpp's tick) reads a rect or a bit from the
    // other state's row.
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
        std::vector<RedesignButton> keys;
        bool level = false;
        for (const IconRowDef& def : run) {
            if (redesign_button_is_level_case(def.id))
                level = true;
            else
                keys.push_back(def.id);
        }
        const int n = static_cast<int>(keys.size());
        int n_whole = 0;
        while (n_whole < n && x + linked_group_w_px(n_whole + 1) <= left_limit)
            ++n_whole;
        paint_group(keys, x, n_whole);
        if (n > 0) x += linked_group_w_px(n) + seats.gap;
        if (level) {
            // THE LEVEL CASE (architect 2026-11-05; Arc's entry since
            // 2026-11-19, send Z): its box the entry a run gap after the
            // group, the key's height, noise_entry_w_px wide — the published
            // rect, the hit box (no seam: it is no linked key) and the
            // damage — its own painter (paint_level_case: the entry, the
            // level's progress line and its number, no lit or press look).
            const RedesignButton id = RedesignButton::IconNoiseLevel;
            const GuiRect entry{x, btn_y, noise_entry_w_px(), icon_case_h_px()};
            if (entry.x + entry.w > left_limit) {
                (void)publish_button_face(cr, app, audio, playback,
                                          target_render, id, GuiRect{});
            } else {
                AppState::RedesignButtonFace& face = publish_button_face(
                    cr, app, audio, playback, target_render, id, entry, entry);
                if (clip_meets(cr, entry))
                    paint_level_case(cr, entry, face.enabled,
                                     app.device_config->noise_level);
            }
            x += entry.w + seats.gap;
        }
    }
    cairo_restore(cr);

    // THE RIGHT RUN, PAINTED LAST AND WHOLE, ONE LINKED GROUP: the history
    // opener at one x in both states, Source / Target and Warp / Phase Reset
    // linked behind it (kIconRowRightRun). NONE IS EVER HIDDEN
    // (history_mode_hides_button): bare `h` is the view's own vocabulary and
    // bare 1 / 2 / 3 / 4 its admitted selectors, so the hide does not reach
    // this run.
    {
        std::vector<RedesignButton> ids;
        for (const IconRowDef& def : kIconRowRightRun) ids.push_back(def.id);
        paint_group(ids, seats.right_x, static_cast<int>(ids.size()));
    }

    cairo_restore(cr);
}

// (THE TAB STRIP'S PAINTER — paint_tab_strip, its two roster tabs and their
// labels kTabStripLabels, 2026-10-18 — IS REMOVED with the strip, architect
// 2026-10-22; the active tab's name is row 8's, beside the clock.)

// THE BOTTOM ROW'S BUTTON CLUSTER AND CLOCK — THE UNIFIED BOTTOM ROW (rows 8
// and 9 merged 2026-08-12): permanent — ordinary
// buttons, no touch mode, no flag, no detection. LEFT TO
// RIGHT (architect 2026-09-29, for the right-handed tablet: the most-used
// buttons at the bottom right):
//
//   THE TIMESTAMP, PLAIN TEXT, "  |  " AND THE ACTIVE TAB'S NAME, one run
//   from 14 W (send AA, 2026-11-19; bottom_left_run below; the face and
//   the fixed width at THE TIME FIELDS' block);
//
//   and, PACKED AGAINST THE RIGHT INSET, five groups, each one linked group (send Z), the run
//   gap between two (architect 2026-10-22; five since 2026-11-08, his
//   "least used left, most used right"):
//   THE COPY / PASTE DUO (kPhaseResetPropagateGroup, architect 2026-11-08,
//   session 93) — COPY (bare P, the phase resets' copy, no modifier) and
//   PASTE (Ctrl+P, the paste, its hold Ctrl+Shift+P the state paste), PASTE
//   alone resting grey on a locked tab and under grid iterations (COPY
//   admitted by both locks, architect 2026-11-17, send U);
//   THE SKIPS (kTransportSkipGroup, back 2026-10-24) — GO TO START (bare
//   Home) and GO TO END (bare End), their shifted presses the whole piece's
//   ends;
//   THE MARKER VERBS (kMarkerVerbGroup) — drop (bare `s`), delete (Delete,
//   its shifted press Shift+Delete since 2026-10-29), disable (Ctrl+D, its
//   shifted press Ctrl+Shift+D since 2026-10-29), inherit (Ctrl+N), JUMP TO DEFINING MARKER (Ctrl+J,
//   since 2026-09-29), OPEN TEXT EDITOR (bare Return, since 2026-10-09),
//   ADD TO SELECTION (bare `k`, the sticky ctrl, lit while the mode stands)
//   and TOGGLE VALUE DRAG (bare `[`, since 2026-10-11, lit while armed — the
//   row's two lamps — closing the group again since 2026-11-08, COPY gone to
//   the duo). The
//   four verbs, Open Text Editor and Toggle Value Drag
//   are the row's resting greys on a locked tab; the jump and Add to
//   selection are not, both being navigation (EDIT FLAG and COPY RESOLVED VALUE stood
//   between Toggle inherit and Add to selection until 2026-09-29; Copy was
//   the icon row's from then until 2026-10-14, its act now the jump's shift
//   twin, and Edit Flag was deleted);
//   THE STEP AND THE WALK (kTransportStepWalkGroup, architect 2026-10-18) —
//   DOWN and UP (bare Down/Up), PREVIOUS MARKER (Shift+Tab) and NEXT MARKER
//   (Tab), none admitting a modifier since 2026-11-08;
//   THE NUDGE AND PLAY (kTransportNudgePlayGroup, architect 2026-10-18) —
//   SWITCH TAB (Ctrl+Tab, its shifted press the paired march), CENTER (bare
//   `c`, down from the icon row since 2026-09-29), LEFT and RIGHT (bare
//   Left/Right). THE FOUR ARROWS, a single line and not a d-pad until
//   2026-10-18 and two pairs since, are THE ROW'S HOLD GESTURE: held past
//   the hold beat they repeat their own chords at the platform's
//   key-repeat rate (key_repeat_period_ms) (the record at the arrows' chord-table rows,
//   input_pointer.cpp). The pressed interior is the ordinary arm's, and the
//   hold has no cue of its own — and, THE LAST GROUP'S LAST CASE CLOSING THE
//   ROW, THE ONE PLAY/STOP BUTTON (bare Space, whose GLYPH and TOOLTIP swap
//   on the live audition bit; Shift+Space the A/B audition). The last button's
//   face ends on the well's last column
//   (bottom_row_seats, 2026-10-23).
//
// EVERY BUTTON GREYS WHERE ITS PRESS WOULD BE A NO-OP (the truthful-buttons
// ruling, redesign_button_enabled), the `h` view's derived partition on top.
// TWO MODE TERMS. THE HIDE (architect 2026-10-07): in the `h` view a member whose chord
// the view refuses in every session is NOT PAINTED (history_mode_hides_button,
// input_pointer.cpp) — Play / Stop, Up and Down, the four verbs, Jump to
// Defining Marker, Open Text Editor, Toggle Value
// Drag, and COPY and PASTE (the duo hidden whole, its run gap with it) —
// and publishes an empty rect, the block
// closing up from
// the right margin; the row in there is
// GO TO START and GO TO END (the mode's own absolute jumps), ADD TO
// SELECTION alone, the walk pair, and SWITCH TAB, CENTER, OLDER and NEWER:
// nine cases in four groups (2026-10-24; seven in three while the skips
// were off the row, 2026-10-12 to 2026-10-24; the duo hidden whole since
// 2026-11-08). THE STAND-IN (architect 2026-10-11,
// kTransportHistoryStandIns): in the view Older / Newer take the seats Left
// and Right hold outside it, the nudge pair publishing empty rects (Up and
// Down hidden), and outside it the two publish empty rects. THE STAND-INS DO NOT INHERIT THE
// ARROWS' HOLD-REPEAT (architect 2026-10-11; kToolbarChords, input_pointer.cpp,
// records why). Otherwise no member publishes a
// zero rect except under a modal.
//
// (A CENTERED ESC BUTTON shipped between the groups on row 8's first day and
// was DELETED at the architect's live pass — "looks like a missing button
// with that cross out"; the mid-render CANCEL lives on the RENDER button now.)
//
// THE BUTTONS ARE THE BAND'S (the unification's point — "the same size as
// the other icon buttons", bigger finger targets without stealing waveform
// height): the program's key (70 x key_height, 2026-11-04 night) — read from
// the band's own accessors (render.h's program block) so the rows cannot
// drift apart — on THE ROW'S OWN GROUND, Arc's pad above the case and the
// same pad below it (send Z, 2026-11-19; 7 W each side since 2026-11-20,
// send EE; Cool Edit's row 8 under its dock
// bar, 4 W above and 5 below, 2026-10-09 to then). (Row 8's kdenlive
// transport metrics, its 26-px boxes and
// its ruled separators; Explorer's flat case with WordPad's air and the
// etched separators, 2026-10-06 to 2026-10-09 — git history.)
//
// EVERYTHING ELSE IS THE BAND'S OWN MODEL: the same keys, each group one
// linked group (paint_roster_group), the same dead look. WHO WEARS THE DEAD FACE HERE, re-derived 2026-10-07 — in
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
// painted in the view). NEXT and PREVIOUS
// MARKER grey where their step would land nothing (since 2026-11-08, their
// twins retired). Outside the view the four VERBS
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
// THE LANE'S GROUND IS THE FLAT GROUND (materials.h's head): the
// lane's chrome is paint_bottom_strip's, which lays it and calls the body
// below onto it.

// THE ROW AUTHORS NO METRIC OF ITS OWN (program_spec.h owns every one): the
// cases and glyphs are the band's, the pads Arc's toolbar's, the run gap
// the program's (2026-10-22; the bars and their pads 2026-10-21, Cool
// Edit's grippers and end bars before), and the row's ends the well's
// first and last columns, flush like the band's (bottom_row_seats,
// 2026-10-23). The render
// player's modal row in this lane takes the same case at the same seat
// (modal_controls_seats) and stands its items its own eight W apart on the
// row's ground (paint_modal_dialog's player branch).

// The painter's half of the row's roster: five groups, all in the right
// block and painted left to right in the order the body lays them (the
// COPY / PASTE duo since 2026-11-08, the two skips since 2026-10-24, the
// marker group, the step and walk, the nudge and play with Play last; the
// `h` view's stand-ins seated in the last); each
// table is declared beside its reasons.
// The press claim's chord table (input_pointer.cpp) is the other half; both
// key off the same ids.
struct TransportRowDef {
    RedesignButton id;
};
// THE SINGLE-MARKER VERBS (architect 2026-08-18, "move drop/delete/disable/
// toggle inherit to bottom right row"), the RIGHT BLOCK'S MARKER GROUP: drop
// (bare `s`), delete (`Delete`), the disable toggle (`Ctrl+D`: a disabled
// marker is one the render does not hear) and the inherit toggle (`Ctrl+N` — a
// pass marker links its tempo to its neighbor). They opened their own
// separator-led group in the ICON ROW from 2026-08-12 until this move.
//
// THE GROUP IS FIVE since later the same day, the architect seating ADD TO
// SELECTION himself — "add group selection icon ('Add to Selection') after
// toggle inherit, before the separator". It is the group's one MODE (bare `k`)
// and therefore wears the lit face; the
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
// its long press is Ctrl+Shift+J, Copy Resolved Value, whose
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
// THE GROUP IS EIGHT AGAIN SINCE 2026-11-08 (architect, session 93): COPY
// PHASE RESETS, its tail from 2026-11-07 (down from the band's first run),
// left it for the COPY / PASTE duo, the row's leftmost run
// (kPhaseResetPropagateGroup below); the verbs keep their order, a unit.
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
// THE COPY / PASTE DUO (architect 2026-11-08, session 93: row 8 "least used
// left, most used right"), THE RIGHT BLOCK'S FIRST RUN, before the skips:
// COPY (bare P, the selected run's phase resets into the clipboard) and
// PASTE (Ctrl+P, the clipboard's resets onto the one selected anchor behind
// the paste's question; its long press Ctrl+Shift+P, their enabled /
// disabled state alone). Both acts, no lamp; both grey on a locked tab and
// PASTE under grid iterations; in the `h` view both are hidden (no `p`
// chord on its allowlist), the run going with its gap (row8_block). Their
// roster entries, app_state.h.
constexpr TransportRowDef kPhaseResetPropagateGroup[] = {
    {RedesignButton::IconCopyPhaseResets},
    {RedesignButton::IconPastePhaseResets},
};
// THE SKIP GROUP (architect 2026-10-24: the two skip buttons back as their
// own run at the left of Drop), THE RIGHT BLOCK'S SECOND since 2026-11-08
// (its first until the duo came): GO TO START (bare
// Home, START) and GO TO END (bare End, END), the trim bounds'
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
// value step, one unit — its hold-repeat at their chord rows,
// input_pointer.cpp), PREVIOUS MARKER (Shift+Tab) and NEXT MARKER (the walk:
// Tab; its camera the audio view's), neither admitting a modifier since
// 2026-11-08. (Until 2026-10-18 the marker-walk group was Previous Marker, Next Marker,
// Center and Switch Tab, and Down / Up led the arrow group behind it.)
// A SHIFT- OR CTRL-MODIFIED CHORD MAY HAVE ITS OWN DEDICATED BUTTON since
// 2026-09-29 (architect: "an artificial constraint; the tablet is the only
// real development surface now") — Previous Marker is Next Marker's Shift
// form and Switch Tab its Ctrl form; Previous Marker had merged into the walk
// on 2026-09-22 under the rule that it could not, and the walk's modified
// presses retired 2026-11-08, each having its own key. Down / Up are the verticals here, Left / Right the
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
// march, both chords' one pointer road since 2026-11-08), CENTER (bare `c` — down from the icon row's zoom group since
// 2026-09-29), LEFT and RIGHT (bare Left / Right, the nudge — the playhead
// step) and PLAY / STOP.
// (Until 2026-10-18 Switch Tab and Center closed the marker-walk group and
// this group was the four arrows with Play.)
//
// PLAY / STOP IS THE GROUP'S LAST MEMBER, beside the right nudge with no
// bar or run gap between them (linked, one seam), and so closes the row at the far
// right outside the `h` view (architect 2026-10-12, revising his own ruling
// of the same morning that seated Play at the marker group's head, and
// correcting the one-member group of the hours after it: "no division
// between the Right arrow and Play"; kept by the 2026-10-18 regroup). Play /
// Stop at the far right sits at the screen's edge, where Shift+Play, the A/B
// audition, is quickest to reach; reaching across from the marker verbs to
// stop a scrub is acceptable, because at working zoom his hand moves faster
// than the scanner. (The Go to Start / Go to End pair came back on
// 2026-10-24 at the block's other end, kTransportSkipGroup.) Play and stop are
// ONE stateful button (2026-08-15): its glyph is STOP's while an audition
// runs (redesign_button_icon's stateful overload — Save's and Render's own
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

// THE TIME FIELDS — THE BOTTOM ICON ROW'S TIMESTAMP, THE RENDER PLAYER'S
// POSITION AND LENGTH AND THE SCORE VIEWER'S PAIR — PLAIN TEXT, NO ENTRY
// (architect 2026-11-19, send AA, RULINGS §8 / §9, his pick C 2 of Z_10:
// the timestamp "probably doesn't need to be ... in its own input box. It
// looks a little awkward now"; the overlays' clocks unboxed the same way):
// THE DIGITS IN THE PROGRAM FACE (Regular 12 W, 24 device px on the tablet;
// gui_font.h's table), THE TEXT TONE `text` (M13, 2026-11-02), on the
// LINE-BOX BASELINE the entry's text had (+18 W of the keys' 28;
// line_box_baseline). THE BOTTOM ICON ROW'S stands at its own pen
// (kBottomLeftPenW, render.h; paint_bottom_row_buttons_and_clock); THE
// OVERLAYS' ARE GTK LABELS IN A BOX AT 6 W SPACING (architect 2026-11-18 /
// 19, send Z's design, built in send AA; his "it touches", RULINGS §8): each
// time's SLOT IS THE SPECIMEN'S ADVANCE ALONE, ROUNDED UP TO A WHOLE W
// (time_label_w_px below: "00:00.000" 53.38 W at Regular 12 W, so 54 W,
// 108 device px on the tablet), THE TEXT'S PEN THE SLOT'S LEFT EDGE, and
// the row's neighbors one run gap apart (the controls rows' layouts). The
// digits are
// TABULAR — Liberation Sans's every digit 0.556 em — AND THE RUN IS SHAPED
// WITH KERNING OFF (text_shape::Kerning::Off, architect 2026-11-05: the
// face's GPOS kerns the pair "11" by about 1.2 px at 22 px, so under its
// kerning a time holding "11" ran narrower than the specimen and its later
// digits walked; 11:11.111 measured 3.7 px narrower than 04:49.027 on the
// tablet, session 40) — so every digit slot is one advance and the colon
// and the point stand still as the time runs.
//
// EVERY TIME LABEL PRESSES NOTHING (architect 2026-11-20, send DD: "it no
// longer carries button look"): the bottom left's timestamp is ground like
// the pipe and the name (RedesignButtonFace::contains, PLAY's box its one
// hit), and the overlays' times stand outside the scrub's item and every
// key's box, publishing only their damage rect (ModalDialogGeometry::clock).
//
// EACH FIELD'S WIDTH IS FIXED — ITS WIDEST STRING (architect 2026-10-05:
// "a 1 is skinnier than a 0", nothing in the field or right of it may move as
// the time runs): the widest digit in every digit slot of "DD:DD.DDD"
// (kTimeShape), shaped through the one chokepoint at the live scale with
// kerning off — the widest digit is picked by advance, all ten being 0.556
// em, so the specimen is "00:00.000", the design's own (DESIGN §3). THE
// CELL — the specimen's ink's whole hull, ceil(ink_r − ink_l) whole px,
// standing where the run at the pen puts it — is THE CLOCK'S DAMAGE BOX
// (clock_invalidate_rect, main.cpp); a time whose digit inks reach past the
// specimen's (a "4") puts a sliver beside it, inside the one px of slack the
// damage carries. THE PLAYER'S ROW HOLDS TWO, one run gap apart: its
// scrub is what the fixed terms leave (player_row_layout).
//
// ROW 8'S RUN IS THE TIMESTAMP, A TEXT PIPE AND THE TAB'S NAME (architect
// 2026-11-19, send AA, RULINGS §10; paint_bottom_row_buttons_and_clock).
//
// TWO MINUTE DIGITS, and longer sources TRUNCATE (the ruling and what it costs
// are at format_timestamp, time_format.h). The cell is that format's width and
// no wider.
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

// The time fields' metrics, MEMOISED ON THE SCALE — eleven tiny shaping
// passes (ten digits and the specimen) of THE PROGRAM FACE, every time
// field's (row 8's clock and the render player's two since 2026-10-26
// ~12:10), that answer the same thing on every frame until the scale
// moves — the widths are the face's own. The scale is the whole key: the
// Program face's size is the table's (gui_font.h; the one live size is the
// Keys face's, 2026-10-29 night).
// Single-threaded paint state; the waveform worker never reaches this file's
// text tiers.
//
// THE CELL IS THE SPECIMEN'S INK'S WHOLE HULL, THE SLOTS ITS ADVANCES
// (architect 2026-11-05; kTimeShape's block): `time_w` is the specimen's
// advance sum, shaped with kerning off, so every time is that wide and no
// glyph ever walks; `ink_l` / `ink_r` are its ink's real edges from origin
// 0 (text_shape::ink_left_px / ink_right_px, unrounded), and `cell_w` =
// ceil(ink_r - ink_l) is row 8's cell and each player field's, in whole px
// (layout); each cell is its field's damage box too. Every shaping here is
// text_shape::Kerning::Off, the fields' own option.
struct TimeFieldMetrics {
    int    percent  = -1;     // the scale this was measured at
    double time_w   = 0.0;    // the widest-digit specimen's shaped width
    double ink_l    = 0.0;    // the specimen's first ink's real edge
    double ink_r    = 0.0;    // the specimen's last ink's real edge
    int    cell_w   = 0;      // ceil(ink_r - ink_l), the ink's whole hull
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
        const double w = text_shape::shape_text_run(
                             font, one, text_shape::Kerning::Off).width_px;
        if (w > widest_w) { widest_w = w; widest = d; }
    }
    std::string specimen(kTimeShape);
    for (char& c : specimen) if (c == 'D') c = widest;
    const text_shape::ShapedRun run =
        text_shape::shape_text_run(font, specimen, text_shape::Kerning::Off);
    m.time_w  = run.width_px;
    m.ink_l   = text_shape::ink_left_px(run);
    m.ink_r   = text_shape::ink_right_px(run);
    m.cell_w  = static_cast<int>(std::ceil(m.ink_r - m.ink_l));
    m.percent = font.percent;
    return m;
}

// EVERY OVERLAY TIME LABEL'S SLOT (the one rule at THE TIME FIELDS' block):
// its width the specimen's advance rounded up to a whole W (label_w_px's
// rule over the measured advance, kerning off), and, from its left edge
// `x` on the case's rows, its box.
static int time_label_w_px() {
    const double factor = gui_scale_factor();
    const double adv_w =
        factor > 0.0 ? time_field_metrics().time_w / factor : 0.0;
    return scaled_px(std::ceil(adv_w), 1);
}
static GuiRect time_label_rect(int x, int case_y) {
    return GuiRect{x, case_y, time_label_w_px(), icon_case_h_px()};
}
// ONE OVERLAY TIME LABEL PAINTED: `text` alone (architect 2026-11-19, send
// AA), kerning off, its pen the slot's left edge, on the line-box baseline,
// clipped to the slot.
static void paint_time_text(cairo_t* cr, const GuiRect& slot,
                            const std::string& text) {
    if (text.empty()) return;
    const GuiFont font = gui_font(GuiFace::Program);
    const text_shape::ShapedRun run =
        text_shape::shape_text_run(font, text, text_shape::Kerning::Off);
    cairo_save(cr);
    cairo_rectangle(cr, slot.x, slot.y, slot.w, slot.h);
    cairo_clip(cr);
    set_palette_source(cr, palette().text);
    text_shape::show_shaped_run(cr, run, static_cast<double>(slot.x),
                                entry_baseline(font, slot));
    cairo_restore(cr);
}

// THE BOTTOM ROW'S SEATS — the bottom icon row's and the modal controls
// row's, ONE OWNER (architect 2026-10-23, "shouldn't row 8 take the same
// rule as top strip?" — yes): the LEFT SEAT where a row's content starts and
// the RIGHT SEAT where its right-aligned cluster ends — THE PANEL'S COLUMNS
// (render.h's well_columns) EACH kEdgeInsetW (6 W) INSIDE (architect
// 2026-11-19, send Z; DESIGN §3: the keys' outermost faces 6 W inside the
// panel, Arc's toolbar margin): 12 and 2292 at 200 % on the tablet's 2304 —
// and THE KEYS' ENDS, CASES_LEFT and CASES_RIGHT, where a row's first key's
// box starts and its last key's box ends: the seats themselves (the box,
// border included, is the key, materials.h's THE KEY), both rows'
// outermost boxes 6 W inside the panel, as the band's (icon_row_seats); THE
// CASE the pad under the row's top (icon_case_top_offset_px, 7 W).
// Text and fields (the clock) start on the left seat. The two rows differ
// only in `row`: the bottom icon row's lane (bottom_row_area), or the
// controls row's, the same rows (modal_controls_row_rect, which never lifts
// since send AA's stage D2; modal_controls_seats below reads this). Every
// BUTTON on either row is Arc's button (materials.h's THE KEY): a group of
// glyph keys one linked group, a word button its own text button.
struct BottomRowSeats {
    int left_x      = 0;   // the row's first column
    int right_x     = 0;   // one past the row's right-aligned cluster
    int cases_left  = 0;   // the row's first face's first column (left_x)
    int cases_right = 0;   // one past the row's last face (right_x)
    int case_y      = 0;   // the face's top: the row's top plus the pad
    int case_w      = 0;   // the program's key, both rows' every button
    int case_h      = 0;
};
static BottomRowSeats bottom_row_seats(const AppState& app,
                                       const GuiRect& row) {
    const WellColumns well = well_columns(app);
    const int l = well.x + edge_inset_px();
    const int r = well.end - edge_inset_px();
    return BottomRowSeats{l, r, l, r, row.y + icon_case_top_offset_px(),
                          icon_case_w_px(), icon_case_h_px()};
}

// THE MODAL FAMILY'S CONTROLS ROW'S SEATS (architect 2026-10-14,
// modal_box.h): every modal's controls stand on the controls row at the
// bottom icon row's case seat, their OUTERMOST FACES 6 W INSIDE THE PANEL
// (THE EDGE RULE, send Z) —
// row 8's own seats (bottom_row_seats, the one owner) on the
// controls row's rows (modal_controls_row_rect, the lane table's, modal_box.h).
static BottomRowSeats modal_controls_seats(const AppState& app) {
    return bottom_row_seats(app, modal_controls_row_rect(app));
}

// ROW 8'S RIGHT BLOCK (2026-10-21: lifted out of
// paint_bottom_row_buttons_and_clock; the numbers and the z rule at the
// painter's THE NUMBERS). THE FIVE GROUPS IN
// PAINTED ORDER, each read off its own table, so a box joining or leaving
// moves the block with no second edit; IN THE `h` VIEW the nudge pair's
// seats are the walk's two steps (kTransportHistoryStandIns, its one owner,
// seated through transport_row_seated). FIVE GROUPS since 2026-11-08, the
// COPY / PASTE duo first, then the skips (architect, session 93: "least
// used left, most used right").
constexpr int kRow8GroupCount = 5;
constexpr std::span<const TransportRowDef> kRow8Groups[kRow8GroupCount] = {
    kPhaseResetPropagateGroup, kTransportSkipGroup, kMarkerVerbGroup,
    kTransportStepWalkGroup, kTransportNudgePlayGroup};
// THE ROW'S LENGTHS (the simple layout, 2026-10-22; Arc's linked groups
// since 2026-11-19, send Z): the run gap between two groups (render.h's
// program_run_gap_px, the `run_gap` tuning key, 6 W its default), each group
// a linked group (materials.h's linked_group_w_px: its keys sharing one
// seam column each).
struct Row8Lengths {
    int gap = 0;
};
static Row8Lengths row8_lengths() {
    return Row8Lengths{program_run_gap_px()};
}
// THE BLOCK, MEASURED under the view's bit `history`: EACH GROUP'S STANDING MEMBERS (the hide,
// history_view_hides_button, asked of what the seat paints), whether any
// group stands, and the block's left edge. THE BLOCK'S WIDTH IS THE PAINTED
// GROUPS' (architect 2026-10-07): in the `h` view a button whose chord the
// view refuses in every session is not painted (history_mode_hides_button,
// input_pointer.cpp), so the count is of the members that stand, and a
// group stands only while it keeps a member. Outside the view nothing is
// hidden and this is every table's whole length. EACH STANDING GROUP IS
// ONE LINKED GROUP of its standing members (linked_group_w_px, materials.h;
// send Z, 2026-11-19), the run gap between two, the last box ending at the
// row's cases_right (6 W inside the panel's right edge).
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
            block_w += linked_group_w_px(b.standing[gi]);
            ++groups;
        }
    }
    if (groups > 1) block_w += (groups - 1) * l.gap;
    b.x = bottom_row_seats(app, lane).cases_right - block_w;
    return b;
}
// THE BLOCK'S ONE WALK, left to right from its left edge: per standing
// group ITS STANDING MEMBERS AS ONE LINKED GROUP (`group(x, ids)`, `x` its
// first box's left column, `ids` the seated members in order; materials.h's
// linked_group_w_px), the run gap before every group but the first; each
// hidden member (`hidden(def)`) wherever it falls.
template <class Group, class Hidden>
static void row8_block_walk(bool history, const Row8Block& b,
                            const Row8Lengths& l, Group&& group,
                            Hidden&& hidden) {
    int ax = b.x;
    bool any_open = false;
    for (int gi = 0; gi < kRow8GroupCount; ++gi) {
        std::vector<RedesignButton> ids;
        for (const TransportRowDef& seat : kRow8Groups[gi]) {
            const TransportRowDef& def = transport_row_seated(seat, history);
            if (row8_hides(history, seat)) {
                hidden(def);
                continue;
            }
            ids.push_back(def.id);
        }
        if (ids.empty()) continue;
        if (any_open) ax += l.gap;
        any_open = true;
        group(ax, ids);
        ax += linked_group_w_px(static_cast<int>(ids.size()));
    }
}

// THE BOTTOM ICON ROW'S LEFT RUN (architect 2026-11-19, send AA, RULINGS
// §9 / §10 and the brief's §T; his picks C 2 of Z_10 and "tabs next to the
// bottom left timestamp, but we separate them with a pipe. So non-bold for
// the timestamp, pipe, and then tab A or tab B"): ONE REGULAR 12 W RUN
// (GuiFace::Program) in the text tone — THE TIMESTAMP (kerning off, THE
// TIME FIELDS' block), then "  |  " (the status bar's own field separator,
// kStatusFieldSeparator) and THE ACTIVE TAB'S NAME — NO ENTRY: "probably
// doesn't need to be ... in its own input box". ITS PEN kBottomLeftPenW
// (14 W, render.h; "And then we put the timestamp line at 14W") from the
// panel's left edge — the status bar's column under it (kStatusBarPenW,
// the same 14 W by its own ruling) — ON THE BASELINE THE ENTRY'S TEXT HAD:
// the 14 W line box centered on the keys' 28 W rows (line_box_baseline,
// +18 W of the case), so with the row's 7 W above its keys and 7 W below,
// 14 W of the row above the line box and 14 W under it. THE PIPE'S PEN
// STANDS AT THE TIMESTAMP SPECIMEN'S ADVANCE ("00:00.000", time_w), so the
// pipe and the name never move as the digits run. THE TIMESTAMP'S OWN BOX
// (`time_box`): the specimen's advance from the pen by the line box's rows,
// real edges — the damage cell's ground (a box of no press: the run presses
// nothing, architect 2026-11-20, send DD). The row's painter's one layout.
struct BottomLeftRun {
    double   pen      = 0.0;   // the timestamp's pen
    double   pipe_pen = 0.0;   // the "  |  <name>" run's pen
    double   baseline = 0.0;
    GuiRectF time_box{};       // the specimen's advance by the line box
};
static BottomLeftRun bottom_left_run(const AppState& app, const GuiRect& lane) {
    const BottomRowSeats seats = bottom_row_seats(app, lane);
    const GuiFont font = gui_font(GuiFace::Program);
    const GuiLineBoxW lb = gui_font_line_box_w(font);
    const TimeFieldMetrics& m = time_field_metrics();
    BottomLeftRun r;
    r.pen = static_cast<double>(well_columns(app).x +
                                scaled_px(kBottomLeftPenW));
    r.baseline = line_box_baseline(font, static_cast<double>(seats.case_y),
                                   key_height_w());
    r.pipe_pen = r.pen + m.time_w;
    const double top = r.baseline - static_cast<double>(scaled_px(lb.ascent));
    r.time_box = GuiRectF{r.pen, top, m.time_w,
                          static_cast<double>(scaled_px(lb.ascent) +
                                              scaled_px(lb.descent))};
    return r;
}
// THE TIMESTAMP'S DAMAGE CELL (the painter's publication, the rule there):
// the whole pixels of the specimen's advance united with its ink, one px of
// slack each side, by the line box's whole rows.
static GuiRect bottom_left_clock_cell(const BottomLeftRun& r) {
    const TimeFieldMetrics& m = time_field_metrics();
    const int c0 = static_cast<int>(
                       std::floor(r.pen + std::min(0.0, m.ink_l))) - 1;
    const int c1 = static_cast<int>(
                       std::ceil(r.pen + std::max(m.time_w, m.ink_r))) + 1;
    const int y0 = static_cast<int>(std::floor(r.time_box.y));
    const int y1 =
        static_cast<int>(std::ceil(r.time_box.y + r.time_box.h));
    return GuiRect{c0, y0, c1 - c0, y1 - y0};
}

void GuiPaintHandler::paint_bottom_row_buttons_and_clock(cairo_t* cr) {
    const GuiRect lane = bottom_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    cairo_save(cr);
    // THE BODY TONE (paint_bottom_strip's ground) under the block and
    // between its groups; the left run as plain text (bottom_left_run); the
    // keys ARC'S LINKED BUTTONS, each group one ring (paint_roster_group,
    // materials.h's paint_linked_group) with their drawings on them
    // (paint_key_glyph).

    // The row's seats (bottom_row_seats): the band's case at the pad
    // under the lane's top.
    const BottomRowSeats seats = bottom_row_seats(app, lane);
    const int btn_y     = seats.case_y;
    // EACH GROUP ONE LINKED GROUP (send Z, 2026-11-19; DESIGN §3): its keys
    // sharing one seam column each inside one bordered ring, the run gap of
    // bare body tone between two groups (row8_block).
    const Row8Lengths lengths = row8_lengths();

    // ONE GROUP, the band's own composition (paint_roster_group): each
    // member publishes its box (linked_key_box) and its seam — the published
    // hit box, the painted extent the publication's coverage test reads,
    // the damage of its drift and its press — and the group paints where the
    // clip reaches it. THE CHECKED KEYS ON THIS ROW are ADD TO SELECTION
    // (2026-08-18, the sticky-ctrl mode that closes the marker-verb group)
    // and TOGGLE VALUE DRAG beside it, each lit in the accent while its mode
    // stands (the radio class retired 2026-11-19, send Z: "we can let go of
    // the green"); a checked key a mode greys wears Arc's lit-and-inert
    // look, the band's own composition.
    const auto paint_group = [&](int gx, const std::vector<RedesignButton>& ids) {
        for (int i = 0; i < static_cast<int>(ids.size()); ++i) {
            const GuiRect box = linked_key_box(gx, btn_y, i);
            (void)publish_button_face(cr, app, audio, playback, target_render,
                                      ids[static_cast<std::size_t>(i)], box,
                                      box, linked_key_seam_px(i));
        }
        const GuiRect group{gx, btn_y,
                            linked_group_w_px(static_cast<int>(ids.size())),
                            icon_case_h_px()};
        if (!clip_meets(cr, group)) return;
        paint_roster_group(cr, app, audio, gx, btn_y, ids);
    };

    // THE ROW, LEFT TO RIGHT (architect 2026-09-29, the right-handed tablet's
    // layout: the most-used buttons at the bottom right; the simple layout
    // 2026-10-22): THE LEFT RUN — the timestamp, a text pipe and THE
    // ACTIVE TAB'S NAME, plain text from kBottomLeftPenW (bottom_left_run)
    // — then THE
    // RIGHT BLOCK, packed against the row's right seat (bottom_row_seats, 6 W
    // inside the panel's right edge), FIVE GROUPS (kRow8Groups, 2026-11-08): the COPY / PASTE
    // DUO (kPhaseResetPropagateGroup), the SKIP
    // GROUP (START | END, kTransportSkipGroup), the MARKER-VERB GROUP (its
    // eight counted off kMarkerVerbGroup), the STEP-AND-WALK GROUP (↓ ↑,
    // Previous Marker and Next Marker, kTransportStepWalkGroup) and the
    // NUDGE-AND-PLAY GROUP with Play / Stop last (Switch Tab, Center, ← →
    // Play, kTransportNudgePlayGroup; architect 2026-10-18), each ONE LINKED
    // GROUP, the run gap between two. The whole block is measured first and laid left
    // to right from there, so one expression owns the anchor and no group
    // re-derives it.
    // ITS TWO MODE TERMS are the `h` view's: the hide (architect 2026-10-07,
    // history_mode_hides_button) — a member hidden there publishes an empty
    // rect, the block is measured over the members that stand, and a group
    // hidden whole takes its gap with it — and the stand-in (architect
    // 2026-10-11, kTransportHistoryStandIns): Older / Newer in the nudge
    // pair's seats; every other button on this row publishes a real rect on
    // every frame, except while an overlay's row holds the row
    // (modal_owns_bottom_row) — where the row yields whole
    // (paint_bottom_strip).
    //
    // THE NUMBERS (send Z, 2026-11-19: Arc's linked groups; in the tablet's
    // device px at 200 %, one W = 2), AT THE DEFAULTS — the 36-W box of
    // key_width 36, a group's keys sharing one seam column each (72 + 70 (n −
    // 1) a group of n, linked_group_w_px), the 6-W run gap of run_gap 6: the
    // block is 21 keys in five groups (the duo two, the skips two, the verbs
    // eight, the step-and-walk four, the nudge-and-play five), 142 + 142 +
    // 562 + 282 + 352 + 4 · 12 = 1528 px, its last box ending at the row's
    // right seat (bottom_row_seats' cases_right, 2292, 6 W inside the
    // panel's right edge) — the groups on x 764..905, 918..1059, 1072..1633,
    // 1646..1927 and 1940..2291 (Play 2220..2291), the kit's seats
    // (tmp/sendZ/mocks/build_log.txt); the left run from x 28 (kBottomLeftPenW,
    // send AA): the timestamp's advance 106.77 px (the 53.4-W advance of
    // "00:00.000" in the Regular 12 W, kerning off), its cell x 27..135, the
    // text pipe's run "  |  Tab B" from x 134.77, about 97 px. IN THE `h`
    // VIEW (the hide, 2026-10-07; Play, Open Text Editor, Toggle Value Drag,
    // COPY and PASTE hidden there with the verbs — the duo whole, its run
    // gap with it — Down and Up with them) the block is the two skips, one
    // verb, the walk pair and Switch Tab, Center and Older / Newer in the
    // nudge pair's seats, 142 + 72 + 142 + 282 + 3 · 12 = 674 px, from x
    // 1618. THE WIDTH AND THE GAP TUNE THE BLOCK (the `key_width` and
    // `run_gap` tuning keys): 42 · key_width − 32 + 8 · run_gap device px,
    // from x 2324 − 42 · key_width − 8 · run_gap — at run_gap 6 it clears the
    // timestamp's cell (x 27..135) up to the grammar's ceiling, 50 W (x 176),
    // and walks over the tab's name from about key_width 49; at key_width 36
    // and run_gap's ceiling, 44 W, it stands from x 460. THE ROW CARRIES NO
    // COLLISION RULE — none of the redesign does — and the crop-at-the-floor
    // allowance recorded at kMinWindowWidthPx covers a narrow window.
    //
    // THE BLOCK WINS IN Z (architect 2026-10-14, on a restored laptop window
    // about 392 W wide: "icons row should win in z-index … msg goes under it.
    // It should be fully opaque"): the block is MEASURED here, ahead of the
    // clock, but it
    // is PAINTED LAST — after the clock and the name, on its own span of the
    // ground laid fresh — so where the window is narrow enough for it to walk
    // over the clock, the block is what shows, whole and opaque, and nothing
    // of the left run comes through. The press agrees by
    // construction: the whole left run — the timestamp, the pipe and the
    // name — is pointer-inert ground (the timestamp since architect
    // 2026-11-20, send DD: "it no longer carries button look"), and the
    // press resolves against the keys' published rects alone
    // (claim_button_row_press, input_pointer.cpp). Every repaint of the clock
    // is this body run whole under the damage clip (the clock cell's rect,
    // clock_invalidate_rect, main.cpp — there is no field-only path), so the
    // per-frame clock damage repaints the block over the clock inside the
    // clip too and cannot put the clock back on top mid-play. At a width
    // where nothing overlaps the order changes no byte: the clock and the
    // name lie wholly left of the block, and the block's fresh
    // ground is paint_bottom_strip's own flat ground over the same span.
    // THE FIVE GROUPS IN PAINTED ORDER, each read off its own table, so a
    // box joining or leaving moves the block with no second edit.
    // IN THE `h` VIEW the nudge pair's seats are the walk's two steps
    // (kTransportHistoryStandIns, its one owner, seated through
    // transport_row_seated): the side that does not stand publishes its
    // empty rects here.
    const bool history = app.history_mode.active;
    // THE BLOCK, MEASURED (row8_block: the standing members, the left
    // edge): the block's painting (after the clock) starts from it.
    const Row8Block block = row8_block(app, history, lane);
    const int right_block_x = block.x;
    for (const TransportStandIn& s : kTransportHistoryStandIns)
        (void)publish_button_face(cr, app, audio, playback, target_render,
                                  history ? s.seat : s.stand_in.id,
                                  GuiRect{});

    // THE BOTTOM LEFT: THE TIMESTAMP, A TEXT PIPE AND THE TAB'S NAME AS ONE
    // RUN, NO ENTRY (architect 2026-11-19, send AA; the seats and their
    // rules at bottom_left_run above), in THE PROGRAM FACE (Regular 12 W),
    // the text tone, inside the save/restore this body already opened. The
    // render player's two fields share its face and its metrics
    // (time_field_metrics); THE TWO NEVER PAINT IN THE SAME FRAME: the
    // player owns the row whole while it stands.
    {
        const GuiFont font = gui_font(GuiFace::Program);
        const BottomLeftRun run = bottom_left_run(app, lane);
        // THE LEFT RUN PRESSES NOTHING (architect 2026-11-20, send DD: "it
        // no longer carries button look"): the timestamp is plain text since
        // send AA, a time label, so it is ground like the pipe and the name —
        // no press, no hover, no tooltip; PLAY is pressed from its own box
        // alone (RedesignButtonFace::contains). The pipe and the name are
        // pointer-inert ground too (no press target: the TAB key keeps its
        // walk, LOCK the lock's one road; RULINGS §10). The digits are never
        // greyed. The render player's and the score viewer's time labels
        // share the face and press nothing either.

        // PUBLISH THE CELL FOR THE DAMAGE OWNER (clock_invalidate_rect,
        // main.cpp — the stash contract is at the field): THE TIMESTAMP'S
        // OWN BOX'S WHOLE PIXELS — the specimen's advance united with its
        // ink's real edges (time_field_metrics), one pixel of slack on each
        // side (another time's glyph may put a sliver outside the
        // specimen's ink, a "4", THE TIME FIELDS' block), by the line box's
        // rows, which hold every digit's ink on its baseline. THE RECT
        // FOLLOWS THE RUN BECAUSE IT IS BUILT FROM THE SAME PEN AND METRICS,
        // so the damage can never miss the painted digits; the pipe and the
        // name, which never move as the digits run (their pen the
        // specimen's advance), stand outside it — a tab switch or a load
        // damages the lane whole (Viewport::invalidate_tab_name_area).
        const GuiRect cell = bottom_left_clock_cell(run);
        app.clock_cell_rect = cell;
        const bool clock_drawn = clip_meets(cr, cell);

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

        // THE RUN. THE RIGHT BLOCK, painted after it, covers it where a
        // narrow window lays the block over this ground (the z rule,
        // 2026-10-14). THE TIMESTAMP CARRIES NO DIRTY MARK (architect
        // 2026-10-05): SAVE'S GREY IS THE MARK (plain_save_actionable,
        // app_state.h). THE ROW YIELDS WHOLE TO AN OVERLAY'S ROW AND TO A
        // PAGE'S ANSWERS, so the run is hidden while the render player, the
        // picker or the score viewer stands or a question's page or the
        // progress page does (modal_owns_bottom_row), and hidden under the
        // on-screen keyboard, which every editor raises over the row (send
        // EE, paint_bottom_strip's yield); the status bar under the row
        // stays but under the keyboard (paint_status_bar).
        // WITH NO PIECE THERE IS NO TIMESTAMP AND NO NAME (architect
        // 2026-10-27, the open's hold's one frame, platform.h's head: no tab
        // of no piece is named; no_audio_to_dispatch_on), so no pipe.
        const std::string time_text = clock_time_text(app, audio);
        const bool no_piece =
            no_audio_to_dispatch_on(app, audio.total_frames());
        const std::string name_text = no_piece                    ? ""
                                      : app.active_tab_view == 'B' ? "Tab B"
                                                                   : "Tab A";
        set_palette_source(cr, palette().text);
        if (clock_drawn && !time_text.empty()) {
            const text_shape::ShapedRun t = text_shape::shape_text_run(
                font, time_text, text_shape::Kerning::Off);
            text_shape::show_shaped_run(cr, t, run.pen, run.baseline);
        }
        if (!name_text.empty()) {
            const std::string tail =
                time_text.empty()
                    ? name_text
                    : std::string(kStatusFieldSeparator) + name_text;
            const text_shape::ShapedRun t =
                text_shape::shape_text_run(font, tail);
            text_shape::show_shaped_run(cr, t, run.pipe_pen, run.baseline);
        }
    }

    // THE RIGHT BLOCK, PAINTED LAST (the z rule at THE NUMBERS above): its
    // span of the lane laid in the ground first — the fill paint_bottom_strip
    // lays, so at a width where nothing overlaps it rewrites the ground that
    // is already there — then the groups left to right on it, so no pixel of
    // the clock's field survives under the block.
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
            [&](int gx, const std::vector<RedesignButton>& ids) {
                paint_group(gx, ids);
            },
            [&](const TransportRowDef& def) {
                (void)publish_button_face(cr, app, audio, playback,
                                          target_render, def.id, GuiRect{});
            });
    }

    cairo_restore(cr);
}

// THE STATUS BAR'S PAINTER (render.h's status bar block, every length and
// tone there; architect 2026-11-19, send Z): the lane's first hairline in
// `well_border` (the line Thunar draws over its bar), the bar under it in the
// body tone `body_face`, and the string (status_bar_text) in the Regular 12
// W (GuiFace::Body) in `text`, its PEN kStatusBarPenW from the panel's left
// edge, ON THE LINE-BOX BASELINE (+15 W of the 22), clipped to the bar (a
// line too long for the panel is cut at its right edge). Painted on every
// frame its lane is exposed on, under every overlay and modal — BUT NOT
// WHILE THE ON-SCREEN KEYBOARD STANDS, which covers the lane whole: the
// painter yields, nothing painted and no record written (architect
// 2026-11-20, send EE: "The status bar should just be obscured by the OSK";
// onscreen_keyboard.h's placement block), its comparator silent with it,
// and the keyboard's hide damages its whole surface, the lane among it, so
// the bar comes back current. ITS AS-PAINTED RECORD
// (AppState::status_bar_painted)
// is written when the clip covers the whole lane, so a partial repaint under
// a changed string leaves the record old and the tick's comparator damages
// the lane again (refresh_status_bar, main.cpp).
void GuiPaintHandler::paint_status_bar(cairo_t* cr) {
    if (onscreen_keyboard::stands(app)) return;
    const GuiRect lane = status_bar_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;
    const GuiPalette& p = palette();
    const int lw = std::min(hairline_px(), lane.h);
    const GuiRect bar{lane.x, lane.y + lw, lane.w, lane.h - lw};
    const std::string text = status_bar_text(app);
    cairo_save(cr);
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        paint_cell_rect(cr, GuiRect{lane.x, lane.y, lane.w, lw},
                        p.well_border);
        paint_cell_rect(cr, bar, p.body_face);
        cairo_restore(cr);
    }
    if (!text.empty() && bar.h > 0) {
        const GuiFont font = gui_font(GuiFace::Body);
        const text_shape::ShapedRun run = text_shape::shape_text_run(font, text);
        const double baseline = line_box_baseline(
            font, static_cast<double>(bar.y), status_bar_w());
        cairo_save(cr);
        cairo_rectangle(cr, bar.x, bar.y, bar.w, bar.h);
        cairo_clip(cr);
        set_palette_source(cr, p.text);
        text_shape::show_shaped_run(
            cr, run,
            static_cast<double>(lane.x + scaled_px(kStatusBarPenW)),
            baseline);
        cairo_restore(cr);
    }
    if (clip_covers_drawable(cr, app, lane)) {
        app.status_bar_painted       = text;
        app.status_bar_painted_valid = true;
    }
    cairo_restore(cr);
}

// -- The floating surfaces ---------------------------------------------------

namespace {

// A ROUNDED BOX'S PATH, a new sub-path: [x0, x1] x [y0, y1], every corner a
// quarter circle of radius `r` (0 a square corner) — the floating surfaces'
// one shape (the tooltip and its shadow's mask, the cards).
void floating_box_path(cairo_t* cr, double x0, double y0, double x1,
                       double y1, double r) {
    const double half_pi = std::numbers::pi / 2.0;
    cairo_new_sub_path(cr);
    if (r <= 0.0) {
        cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
        return;
    }
    cairo_arc(cr, x1 - r, y0 + r, r, -half_pi, 0.0);
    cairo_arc(cr, x1 - r, y1 - r, r, 0.0, half_pi);
    cairo_arc(cr, x0 + r, y1 - r, r, half_pi, 2.0 * half_pi);
    cairo_arc(cr, x0 + r, y0 + r, r, 2.0 * half_pi, 3.0 * half_pi);
    cairo_close_path(cr);
}

// A FLOATING SURFACE'S BOX (send AA's stage D1, 2026-11-19: Arc's tooltip
// and notifyd's bubbles, un-shared — each painter its own look): the whole
// box `r` rounded by `radius`, filled in `fill` AT `alpha` OVER WHAT LIES
// UNDER IT (set_palette_source_alpha: the chrome blended here, the canvas
// by the GPU inside the floaters' hulls — canvas_sink.h's THE TRANSPARENCY
// INVARIANT), at cairo's default antialiasing; no border, no ring.
void paint_floating_box(cairo_t* cr, const GuiRect& r, double radius,
                        GuiColor fill, double alpha) {
    if (r.w <= 0 || r.h <= 0) return;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_path(cr);
    floating_box_path(cr, r.x, r.y, r.x + r.w, r.y + r.h, radius);
    set_palette_source_alpha(cr, fill, alpha);
    cairo_fill(cr);
    cairo_restore(cr);
}

// GTK 3.24'S BOX BLUR, transcribed (gtkcairoblur.c — blur_xspan, blur_rows,
// flip_buffer, _boxblur; the fetched copy tmp/sendZ/mock3/src_fetch/): one
// pass of width `d` over a row of `n` bytes, the window ending `offset`
// past each output (d / 2 for odd d, (d − shift) / 2 for even), zeros
// outside the row, each output (sum + d / 2) / d in integers.
void gtk_blur_xspan(uint8_t* row, uint8_t* tmp, int n, int d, int shift) {
    const int offset = (d % 2 == 1) ? d / 2 : (d - shift) / 2;
    int sum = 0;
    for (int i = -d + offset; i < n + offset; ++i) {
        if (i >= 0 && i < n) sum += row[i];
        if (i >= offset) {
            if (i >= d) sum -= row[i - d];
            tmp[i - offset] = static_cast<uint8_t>((sum + d / 2) / d);
        }
    }
    std::memcpy(row, tmp, static_cast<size_t>(n));
}
// THREE PASSES a row (blur_rows): odd d three centered; even d (d, +1),
// (d, −1), then (d + 1) centered — a symmetric blur, the SVG technique GTK
// names.
void gtk_blur_rows(uint8_t* buf, uint8_t* tmp, int width, int height,
                   int d) {
    for (int i = 0; i < height; ++i) {
        uint8_t* row = buf + static_cast<size_t>(i) * width;
        if (d % 2 == 1) {
            gtk_blur_xspan(row, tmp, width, d, 0);
            gtk_blur_xspan(row, tmp, width, d, 0);
            gtk_blur_xspan(row, tmp, width, d, 0);
        } else {
            gtk_blur_xspan(row, tmp, width, d, 1);
            gtk_blur_xspan(row, tmp, width, d, -1);
            gtk_blur_xspan(row, tmp, width, d + 1, 0);
        }
    }
}
// _boxblur with GTK_BLUR_X | GTK_BLUR_Y: the columns first (the buffer
// transposed, its rows blurred, transposed back), then the rows; the
// buffer `width` bytes a row (GTK hands the A8 surface's stride).
void gtk_boxblur(uint8_t* buf, int width, int height, int d) {
    if (d <= 1 || width <= 0 || height <= 0) return;
    std::vector<uint8_t> flipped(static_cast<size_t>(width) * height);
    std::vector<uint8_t> tmp(static_cast<size_t>(std::max(width, height)));
    for (int i = 0; i < width; ++i)
        for (int j = 0; j < height; ++j)
            flipped[static_cast<size_t>(i) * height + j] =
                buf[static_cast<size_t>(j) * width + i];
    gtk_blur_rows(flipped.data(), tmp.data(), height, width, d);
    for (int i = 0; i < width; ++i)
        for (int j = 0; j < height; ++j)
            buf[static_cast<size_t>(j) * width + i] =
                flipped[static_cast<size_t>(i) * height + j];
    gtk_blur_rows(buf, tmp.data(), width, height, d);
}

// THE SHADOW'S MASK FOR ONE BOX SIZE, kept: the mask depends on the box's
// size and the scale's lengths alone, and the tooltip repaints under every
// damage that meets it (a scanner passing under it), so the blur runs once
// a box size — the last one held, a new size replacing it.
struct TooltipShadowMask {
    int w = 0, h = 0, spread = 0, offset = 0, d = 0, radius = 0;
    int mw = 0, mh = 0, margin = 0;
    cairo_surface_t* surface = nullptr;
};
TooltipShadowMask& tooltip_shadow_mask_cache() {
    static TooltipShadowMask m;
    return m;
}

// ARC'S TOOLTIP SHADOW AS GTK DRAWS IT (render.h's ARC'S TOOLTIP SHADOW, the
// rule and its numbers): the whole box `box` moved down the offset, grown
// the spread, its radius the box's plus the spread, filled into an A8 mask
// with `margin` px of zeros round it (the blur's reach and one more), the
// mask blurred (gtk_boxblur), then black at kTooltipShadowAlpha painted
// through it under a clip of the mask's rect LESS THE BOX (even-odd, the
// box's own rounded path at cairo's coverage), so nothing lands inside the
// box and the box's fill alone stands there.
void paint_tooltip_shadow(cairo_t* cr, const GuiRect& box, int radius) {
    const int s = tooltip_shadow_spread_px();
    const int o = tooltip_shadow_offset_px();
    const int d = tooltip_shadow_box_px();
    const int margin = tooltip_shadow_blur_reach_px() + 1;
    TooltipShadowMask& m = tooltip_shadow_mask_cache();
    if (!m.surface || m.w != box.w || m.h != box.h || m.spread != s ||
        m.offset != o || m.d != d || m.radius != radius) {
        if (m.surface) cairo_surface_destroy(m.surface);
        m = TooltipShadowMask{};
        m.w = box.w; m.h = box.h; m.spread = s; m.offset = o; m.d = d;
        m.radius = radius; m.margin = margin;
        m.mw = box.w + 2 * s + 2 * margin;
        m.mh = box.h + 2 * s + 2 * margin;
        m.surface = cairo_image_surface_create(CAIRO_FORMAT_A8, m.mw, m.mh);
        cairo_t* c = cairo_create(m.surface);
        cairo_set_antialias(c, CAIRO_ANTIALIAS_DEFAULT);
        floating_box_path(c, margin, margin, margin + box.w + 2 * s,
                          margin + box.h + 2 * s,
                          radius > 0 ? static_cast<double>(radius + s) : 0.0);
        cairo_set_source_rgba(c, 0.0, 0.0, 0.0, 1.0);
        cairo_fill(c);
        cairo_destroy(c);
        cairo_surface_flush(m.surface);
        gtk_boxblur(cairo_image_surface_get_data(m.surface),
                    cairo_image_surface_get_stride(m.surface), m.mh, d);
        cairo_surface_mark_dirty(m.surface);
    }
    const int mx = box.x - s - m.margin;
    const int my = box.y + o - s - m.margin;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_DEFAULT);
    cairo_new_path(cr);
    cairo_rectangle(cr, mx, my, m.mw, m.mh);
    floating_box_path(cr, box.x, box.y, box.x + box.w, box.y + box.h,
                      static_cast<double>(radius));
    cairo_set_fill_rule(cr, CAIRO_FILL_RULE_EVEN_ODD);
    cairo_clip(cr);
    set_palette_source_alpha(cr, GuiColor{0.0, 0.0, 0.0}, kTooltipShadowAlpha);
    cairo_mask_surface(cr, m.surface, mx, my);
    cairo_restore(cr);
}

} // namespace

GuiRect GuiPaintHandler::tooltip_box_for_show() {
    // The contract is at the declaration: the painter's own layout, on a
    // one-pixel target it never draws to.
    cairo_surface_t* target =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(target);
    tooltip_measured_  = GuiRect{0, 0, 0, 0};
    tooltip_measuring_ = true;
    paint_shift_tooltip(cr);
    tooltip_measuring_ = false;
    cairo_destroy(cr);
    cairo_surface_destroy(target);
    return tooltip_measured_;
}

void GuiPaintHandler::paint_shift_tooltip(cairo_t* cr) {
    // THE HOVER TOOLTIP, for the box's own button — at most one box. The
    // tooltip's clock (tick_tooltip) owns WHEN it shows and goes; this owns
    // only what it looks like, and publishes the rect it painted so the hide
    // edge can damage it — AS PAINTED (the rule is at AppState::
    // RedesignTooltip): the previous rect is held until the new box is known.
    // THE RECORD OUTLIVES A RUN THAT PAINTS NO BOX UNLESS THAT RUN ERASED IT
    // (architect 2026-11-16, the beats: the hide edge damages this rect and
    // nothing wider, so the rect must name the box's pixels for as long as
    // any may stand): a run under a clip that covers the recorded rect has
    // repainted every pixel of it, so with no box painted below the record
    // is empty; a run whose clip missed it — a narrow rect elsewhere, while
    // the box is hidden or has nothing to paint — leaves the pixels and so
    // the record. THE MEASURE FORM (tooltip_box_for_show, paint_handler.h)
    // touches no record.
    const GuiRect prev_rect = app.redesign_tooltip.rect;
    if (!tooltip_measuring_ &&
        (prev_rect.w <= 0 || prev_rect.h <= 0 ||
         clip_covers_drawable(cr, app, prev_rect)))
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
    // button the wait ripened on — no tooltip owner is stored anywhere else
    // (the hovered FACE, AppState::FaceFades, is another thing: it names
    // only a key a press would act on, and nothing while a contact is down).
    const AppState::RedesignTooltip::Owner owner = app.redesign_tooltip.owner;
    if (owner.index < 0) return;

    // THE FOUR SURFACES (the encoding is at the field): a ROSTER owner reads
    // the roster's constant/stateful hint table and the roster's painted rect;
    // a DIALOG owner reads the modal stash the painter itself publishes — its
    // composed hint and its button rect, both written by paint_modal_dialog,
    // which runs BEFORE this body precisely so the rect the box stands above
    // is the one this frame draws; a MARKER owner (2026-11-09) reads its cue's
    // recomposed words and the cue's published face; a HISTORY owner
    // (2026-11-11) its item's recomposed words and the item's published face.
    const char* line1 = nullptr;
    const char* line2 = nullptr;
    GuiRect     btn{0, 0, 0, 0};
    // THE STRUCK RUN LEADING LINE 1 — a History owner's older side, or a
    // Marker owner's struck cut line (2026-11-17); null for every other
    // owner and for a run that is absent.
    const char* struck = nullptr;
    // A MARKER OWNER'S RUNS, held here so line1 and struck can point into
    // them.
    MarkerTooltipText marker_text;
    // A HISTORY OWNER'S RUNS, held likewise.
    HistoryCueTooltipText history_text;
    if (owner.surface == AppState::RedesignTooltip::Surface::History) {
        // THE ITEM'S DIFF (architect 2026-11-11, sessions 106 / 107; the
        // History owner at AppState::RedesignTooltip): EXACTLY WHAT THE FLAG
        // SHOWS, ON ONE LINE, through the one owner (history_cue_tooltip_text
        // — the older side's label as the struck run, the newer side's as
        // line 1's plain run, either possibly empty), no hint line; the face
        // the item's in the live stash (history_cue_face).
        history_text = history_cue_tooltip_text(app, owner.index);
        if (history_text.empty()) return;
        if (!history_text.struck.empty()) struck = history_text.struck.c_str();
        line1 = history_text.plain.c_str();
        btn   = history_cue_face(app, owner.index);   // the vertical seat
    } else if (owner.surface == AppState::RedesignTooltip::Surface::Marker) {
        // THE CUE'S WORDS (the Marker owner at AppState::RedesignTooltip):
        // ONE LINE, recomposed through the one owner (marker_tooltip_text —
        // the value copy's payload verbatim as the plain run, architect
        // 2026-11-09, session 100; or a cut label's whole flag line, struck
        // where the lane strikes the label, architect 2026-11-17, send U;
        // empty, and so no box, on every other cue), laid as a History
        // item's two runs below, in the name line's face and ink; no hint
        // line. The face is the cue's in the LIVE stash (marker_cue_face), so
        // a cue that moved is read where it is painted; a cue with no
        // published rect paints nothing, and the walk's next pass ends the
        // box.
        marker_text = marker_tooltip_text(app, audio, owner.index);
        if (marker_text.empty()) return;
        if (!marker_text.struck.empty()) struck = marker_text.struck.c_str();
        line1 = marker_text.plain.c_str();
        btn   = marker_cue_face(app, owner.index);   // the vertical seat
    } else if (owner.surface == AppState::RedesignTooltip::Surface::Dialog) {
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
        // The key's box — tooltip_damage_band reads the same.
        btn   = app.redesign_buttons[owner.index].rect;
    }
    if (btn.w <= 0 || btn.h <= 0) return;

    cairo_save(cr);
    // ARC'S TOOLTIP (architect 2026-11-19, send Z, his "good to build" on
    // tmp/sendZ/VIEW/Z_07; render.h's THE TOOLTIP IS ARC'S, the rule and its
    // numbers): THE LINES Regular 12 W — the name line in GuiFace::Program,
    // the hint line in GuiFace::Hint, both 12 W Regular — BOTH IN
    // `sidebar_text` (Arc's tooltip label, BAC3CF; the hint line the same
    // size and tone, Arc printing one label), each line one line box
    // (gui_font_line_box_w, 11 + 3 W) under the pads; THE BOX the carried
    // fill `tooltip_fill` at its opacity over what lies under it, radius
    // kTooltipRadiusW, no border; ARC'S SHADOW outside it
    // (paint_tooltip_shadow above).
    //
    // THE BOX IS WHOLE PX, SIZED BY ITS LINE BOXES (the line-box rule,
    // render.h's line_box_baseline): kTooltipPadW above and below the
    // lines' line boxes, which stack one on the other, and kTooltipPadW
    // either side of the wider line's advance, ceiled — so one line is
    // 10 + 14 + 10 = 34 W tall (68 device rows on the tablet) and two
    // 10 + 14 + 14 + 10 = 48 W (96). THE BASELINES each its line box's
    // ascent under that box's top: +21 W, then +14 W a line. THE PEN one pad
    // in from the box's real left edge (`box`, exact: a cue's box seats its
    // text origin on the label's pen, below); the box is painted on its hull.
    //
    // A HISTORY ITEM'S ONE LINE (architect 2026-11-11, session 107: "both in
    // one line with the old one struck and the old one listed first") is two
    // runs in the name line's face and ink: the STRUCK run first, its bar the
    // lane's own geometry (struck_run_bar, render.h — the cue labels' strike,
    // the face's stroke on its strikeout rows across the run's advance) in
    // the same ink, then the PLAIN run, its pen the struck run's own
    // advance, unrounded, and the lane's segment step past it
    // (cue_segment_text_gap_px:
    // pad, gap, pad — where the lane's second segment's text stands from the
    // first's). A run that is absent takes no room and no step; line 1's
    // extent is the runs' with the step between. A CUE'S ONE LINE is laid
    // the same (architect 2026-11-17, send U): a struck cut line is the
    // struck run alone, any other words the plain run alone
    // (marker_tooltip_text).
    const GuiFont name_font = gui_font(GuiFace::Program);
    const GuiFont hint_font = gui_font(GuiFace::Hint);
    const bool two_line = (line2 != nullptr);
    const text_shape::ShapedRun r1 =
        text_shape::shape_text_run(name_font, line1);
    const GuiLineBoxW box1 = gui_font_line_box_w(name_font);
    text_shape::ShapedRun rs;
    double line1_w  = r1.width_px;
    double plain_dx = 0.0;   // the plain run's pen past line 1's
    if (struck != nullptr) {
        rs = text_shape::shape_text_run(name_font, struck);
        plain_dx = rs.width_px +
                   static_cast<double>(cue_segment_text_gap_px());
        line1_w  = line1[0] != '\0' ? plain_dx + r1.width_px : rs.width_px;
    }

    double w2 = 0.0;
    text_shape::ShapedRun r2;
    GuiLineBoxW box2;
    if (two_line) {
        r2 = text_shape::shape_text_run(hint_font, line2);
        box2 = gui_font_line_box_w(hint_font);
        w2 = r2.width_px;
    }

    // Each part rounded at the element (scaled_px's head): the pad, each
    // line box, each ascent.
    const int pad     = tooltip_pad_px();
    const int line1_h = scaled_px(box1.line());
    const int line2_h = two_line ? scaled_px(box2.line()) : 0;
    const double w = static_cast<double>(2 * pad) +
                     std::ceil(std::max(line1_w, w2));
    const double h = static_cast<double>(2 * pad + line1_h + line2_h);

    // OFF THE OWNER'S FACE ALONE (architect 2026-11-07, sessions 90 and 91):
    // UNDER a top-row key's face and ABOVE every other (send Z, 2026-11-19:
    // the title bar no longer holds a band key's box), one W between
    // (render.h's kTooltipGapW) — THE GAP MEASURED TO THE BOX, the shadow
    // hanging outside it over the gap and the face — its left edge on
    // `btn`'s — then held inside the panel (the edge rule) and under the
    // window's top, the box never shrinking (a truncated hint would be
    // worse than one that shifted). The rule's one statement is
    // tooltip_box_rect (app_state.h).
    // A CUE'S BOX (architect 2026-11-09, session 100: "the first char on the
    // left of the tooltip should align horizontally with the first char of
    // the marker") seats its TEXT ORIGIN, x + pad below, on the label's
    // pen as the cue pass drew it — marker_tooltip_box_rect, app_state.h,
    // over the same rule, and a history item's the same
    // (history_tooltip_box_rect); `btn` stays its vertical seat.
    const GuiRectF box =
        owner.surface == AppState::RedesignTooltip::Surface::Marker
            ? marker_tooltip_box_rect(app, owner.index, pad, w, h)
        : owner.surface == AppState::RedesignTooltip::Surface::History
            ? history_tooltip_box_rect(app, owner.index, pad, w, h)
            : tooltip_box_rect(app, btn, w, h);
    const double x = box.x;
    const double y = box.y;
    // THE PAINTED BOX IS THE BOX'S HULL (render.h); THE PUBLISHED RECT IS
    // THE PAINTED EXTENT — the hull grown by the shadow's reach
    // (tooltip_painted_extent, render.h), every pixel the tooltip lays
    // down: what the hide edge and the words comparators damage.
    const GuiRect box_hull = hull(box);
    const GuiRect extent   = tooltip_painted_extent(box_hull);
    // THE MEASURE FORM ENDS HERE (tooltip_box_for_show): the extent the
    // show will damage, nothing painted and nothing published.
    if (tooltip_measuring_) {
        tooltip_measured_ = extent;
        cairo_restore(cr);
        return;
    }
    // This box's first paint run is here: its show is pending no longer
    // (AppState::RedesignTooltip::show_pending), whatever the clip below
    // covers.
    app.redesign_tooltip.show_pending = false;
    // PUBLISHED AS PAINTED: a clip that covers the extent draws it whole, so
    // the rect and the words are exactly this frame's; a clip that does not
    // (the player's clock and scrub cells at scanner cadence, which recompose
    // a modal button's words on the stash above without reaching the box)
    // leaves the old box's pixels where they stood, so the rect grows to the
    // union of the two and the words stay the last ones really drawn —
    // main.cpp's comparator sees that drift and damages the box whole.
    if (clip_covers_drawable(cr, app, extent)) {
        app.redesign_tooltip.rect          = extent;
        app.redesign_tooltip.painted_line1 = line1;
        app.redesign_tooltip.painted_line2 = line2 != nullptr ? line2 : "";
        app.redesign_tooltip.painted_struck = struck != nullptr ? struck : "";
    } else {
        app.redesign_tooltip.rect =
            (prev_rect.w > 0 && prev_rect.h > 0)
                ? union_rect(prev_rect, extent)
                : extent;
    }

    // THE SHADOW, THEN THE BOX (the order GTK paints a box-shadow under its
    // background): the shadow outside the box alone, the box's carried fill
    // over whatever lies under it, the chrome on the CPU and the canvas on
    // the GPU (canvas_sink.h's THE TRANSPARENCY INVARIANT).
    const GuiPalette& p = palette();
    const int radius = tooltip_radius_px();
    paint_tooltip_shadow(cr, box_hull, radius);
    paint_floating_box(cr, box_hull, static_cast<double>(radius),
                       p.tooltip_fill,
                       tone_alpha(installed_theme().tones.tooltip_fill));

    // THE BASELINES (the block above): line 1's its line box's ascent under
    // the top pad, line 2's its own ascent under line 1's box; the pen one
    // pad in from the box's real left edge.
    const double base1 =
        y + static_cast<double>(pad + scaled_px(box1.ascent));
    const double pen1 = x + static_cast<double>(pad);
    if (struck != nullptr) {
        set_palette_source(cr, p.sidebar_text);
        text_shape::show_shaped_run(cr, rs, pen1, base1);
        paint_ink_rect(cr, struck_run_bar(name_font, pen1, rs.width_px, base1),
                       p.sidebar_text);
    }
    set_palette_source(cr, p.sidebar_text);
    text_shape::show_shaped_run(cr, r1, pen1 + plain_dx, base1);
    if (two_line) {
        set_palette_source(cr, p.sidebar_text);
        text_shape::show_shaped_run(
            cr, r2, pen1,
            y + static_cast<double>(pad + line1_h + scaled_px(box2.ascent)));
    }

    cairo_restore(cr);
}

namespace {

// (THE CARD'S LINE SPACING, the body face's exact line height, is the line
// box's since send AA's stage D1, 2026-11-19: notification_line_px,
// notifications.h — 14 W, whole.)

// THE WRAP, and the ONE place a card's text is broken into lines (architect
// 2026-08-30). GREEDY WORD WRAP AT ASCII SPACES ON MEASURED ADVANCES: each
// candidate line is shaped as ONE run through the shaping chokepoint and
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
// text the sentence never had). EVERY RUN THROUGH THE LABEL RUNS' MEMO
// (text_shape::memoized_run; send BB, 2026-11-19 — the same pixels, the
// memo's hit the very run a fresh shape returns): the layout runs under
// every on_redraw call while a card stands (paint_notifications: its
// publication is the layout's), so a standing card's sentence and its wrap
// candidates are shaped once and found again at every later run.
std::vector<text_shape::SharedRun> notification_text_lines(
        const GuiFont& font, const std::string& text,
        const text_shape::SharedRun& whole, double room_px) {
    std::vector<text_shape::SharedRun> lines;
    if (room_px <= 0.0 || whole->width_px <= room_px) {
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
            lines.push_back(text_shape::memoized_run(font, all.substr(pos)));
            return lines;
        }
        size_t end = n;
        text_shape::SharedRun line;
        for (size_t probe = pos;;) {
            const size_t sp = all.find(' ', probe);
            const size_t cand_end = (sp == std::string_view::npos) ? n : sp;
            text_shape::SharedRun cand = text_shape::memoized_run(
                font, all.substr(pos, cand_end - pos));
            if (cand->width_px > room_px) break;
            line = std::move(cand);
            end  = cand_end;
            if (cand_end >= n) break;
            probe = cand_end + 1;
        }
        if (!line) {
            const size_t sp = all.find(' ', pos);
            end  = (sp == std::string_view::npos) ? n : sp;
            line = text_shape::memoized_run(font, all.substr(pos, end - pos));
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
// classes, the hit rule, the inventory and THE LOOK at notifications.h). THE
// WHOLE STACK — AppState::Notifications::cards, newest first, every one of
// them on screen since the queue retired 2026-08-30 — painted top-right at
// notifyd's seat, 16 W inside the window's right edge and 16 W under its
// first row (architect 2026-11-19, send BB: "in xfce the card appears at the
// top right of the screen without regard for what it obstructs"; the rule
// and the margins at notification_stack_bound) — over the title bar and the
// top icon row and growing DOWN over whatever lies there (the lanes, the
// waveform), the cards notification_gap_px apart (6 W, 12 device px on the
// tablet).
//
// THE LOOK (architect 2026-11-19, send Z, "good to build" on Z_07; built in
// send AA's stage D1): NOTIFYD'S BUBBLE — a rounded box (kNotificationRadiusW)
// in its class's carried fill AT ITS OPACITY over what lies under it
// (paint_floating_box: a normal card `card_fill`, a standing one
// `card_standing_fill`), no border, no shadow; notifyd's ONE PADDING, 14 W
// (kNotificationPadW, send BB); THE CLASS GLYPH, the `icons` set's
// DialogInformation or DialogError, its whole drawing in a 16 W box at the
// left and top padding (icons::ink_in_box) — then the padding again, then
// THE SENTENCE, Regular 12 W, one line centered on the glyph's box (its
// baseline +26 W), wrapped lines from the top padding (+25 W); the words and
// the glyph one tone, a normal card's `sidebar_text`, a standing card's
// `card_text`. The card wears no hover face and no pressed face.
// The card's width is its content's, no floor (architect 2026-11-19, send AA:
// "agree"), under notification_card_max_w_px — the authored ceiling since
// 2026-08-31, scaled like every other length, with the panel itself as a
// safety under it.
//
// THE TEXT GROWS DOWNWARD (architect 2026-08-30). The
// width rule is unchanged — the WHOLE sentence's shaped width sets it, inside
// the same clamp — so a sentence that fits its room is one line exactly as it
// was; a longer one wraps through notification_text_lines above to at most
// kNotificationMaxLines, the last of which clips at the run's right edge (no
// ellipsis — the folder overlay rows' precedent). THE CARD GROWS, NOTHING
// REFLOWS: the glyph keeps its seat on the top padding, the lines stand from
// the top padding (one line, shorter than the glyph's box, centered on it),
// each further line one line box (14 W) lower, and the card is the taller of
// the glyph's box and its lines between the two paddings
// (notification_card_h_px). The stack's `y` then advances by each card's OWN
// height and the gap.
//
// IT PUBLISHES WHAT IT LAYS OUT, WHICH IS WHAT STANDS: each card's rect,
// and their union, into AppState::Notifications on every run — the card
// drawn by this run where the run's clip meets it, and standing from the
// run that last drew it everywhere else, every change of the stack
// damaging the cards it changed (below). The owner-tag doctrine is at
// ModalDialogGeometry: published geometry may only SELECT; the press claim
// and the hover walk read this and then ask the live stack.
// THE CARDS ARE CLIPPED TO THE ROOM and PUBLISHED CLIPPED TO IT: a stack tall
// enough to reach the bottom row is the contrived case nothing caters for,
// and clipping is what keeps it from painting over that row or claiming a
// press under it. It runs on every frame, cards or none, for the floating
// surfaces' reason: a skipped run would strand a stale publication. Its
// damage is not its own — every stack change damages, through the viewport
// owner, the cards whose rect the change moves, added or took away
// (Viewport::invalidate_notification_stack: the publication here against
// THE MEASURE FORM's layout, notification_stack_laid_out below) — and the
// hover paints nothing to damage.
void GuiPaintHandler::notification_stack_laid_out(
    std::vector<AppState::NotificationPainted>& out) {
    // The contract is at the declaration: the painter's own layout pass, on
    // a one-pixel target it never draws to.
    cairo_surface_t* target =
        cairo_image_surface_create(CAIRO_FORMAT_ARGB32, 1, 1);
    cairo_t* cr = cairo_create(target);
    out.clear();
    notifications_measured_ = &out;
    paint_notifications(cr);
    notifications_measured_ = nullptr;
    cairo_destroy(cr);
    cairo_surface_destroy(target);
}

void GuiPaintHandler::paint_notifications(cairo_t* cr) {
    AppState::Notifications& st = app.notifications;
    // THE MEASURE FORM (notification_stack_laid_out) publishes nothing: the
    // publication stays what the last real run laid out.
    const bool measuring = notifications_measured_ != nullptr;
    if (!measuring) {
        st.painted.clear();
        st.painted_rect = GuiRect{0, 0, 0, 0};
    }
    if (st.cards.empty()) return;

    // THE ROOM the stack grows into, and the clip for every card below. A
    // window with none of it paints no card at all (notifications.h).
    const GuiRect room = notification_stack_bound(app);
    if (room.w <= 0 || room.h <= 0) return;

    cairo_save(cr);
    const GuiFont font = gui_font(GuiFace::Body);

    const int line_px  = notification_line_px();
    const int max_w    = notification_card_max_w_px(app);
    const int gap      = notification_gap_px();
    // THE LAYOUT (notifyd's bubble, notifications.h's lengths; architect
    // 2026-11-19, send Z, DESIGN §9; THE ONE PADDING send BB's, the same
    // day): the padding, the glyph's 16 W box, the padding again, the text's
    // pen; the wrap's room ends the padding short of the box's right edge;
    // the glyph's box on the top padding, the text's top
    // notification_text_top_px (one line centered on the glyph's rows).
    // Every part a whole W through scaled_px, rounded at the element: on the
    // tablet the pen 28 + 32 + 28 = 88 device px in, the right pad 28, the
    // glyph's box rows 28 .. 59 of a one-line card's 88, its line box rows
    // 30 .. 57 and its baseline 30 + 22 = 52 rows down.
    const int pad       = scaled_px(kNotificationPadW, 1);
    const int glyph_box = scaled_px(kNotificationGlyphBoxW, 1);
    const GuiLineBoxW line_box = gui_font_line_box_w(font);
    const int ascent    = scaled_px(line_box.ascent);
    const int glyph_top = pad;
    const double radius = static_cast<double>(scaled_px(kNotificationRadiusW, 0));
    const int    lead_w   = pad + glyph_box + pad;
    const double chrome_w = static_cast<double>(lead_w + pad);
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
        double                               text_x    = 0.0;
        double                               text_room = 0.0;
        std::vector<text_shape::SharedRun>   lines;
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
        const text_shape::SharedRun whole =
            text_shape::memoized_run(font, n.text);
        // THE BOX IS WHOLE PIXELS (a floating surface's, render.h's hull):
        // the ceiling of its real content width, so the text's room is the
        // shaped width or more.
        const int wanted =
            static_cast<int>(std::ceil(chrome_w + whole->width_px));
        // No floor (architect 2026-11-19, send AA, notifications.h): the
        // content's width, under the ceiling, which is also the room this
        // paints into, so no card is wider than its clip.
        const int w = std::min(wanted, max_w);
        const int card_x       = right_x - w;
        const double text_x    = static_cast<double>(card_x + lead_w);
        // The room from the pen to the right pad; a card not clamped under
        // its content holds the whole sentence by the ceiling above (the
        // max keeps the last bit of the two sums from breaking a line).
        const double text_room =
            w >= wanted
                ? std::max(static_cast<double>(w) - chrome_w, whole->width_px)
                : static_cast<double>(w) - chrome_w;
        std::vector<text_shape::SharedRun> lines =
            notification_text_lines(font, n.text, whole, text_room);
        // THE CARD'S HEIGHT, its content the taller of the glyph's box and
        // its lines (notification_card_h_px): a wrapped card grows downward
        // by one line box a line past the glyph's.
        const GuiRect card{
            card_x, y, w,
            notification_card_h_px(static_cast<int>(lines.size()))};
        laid.push_back(LaidCard{&n, card, text_x, text_room, std::move(lines)});
        y += card.h + gap;
    }

    // THE MEASURE FORM ENDS HERE: every laid card's rect as the card pass
    // below would publish it (clipped to the room), nothing painted.
    if (measuring) {
        for (const LaidCard& c : laid)
            notifications_measured_->push_back(
                AppState::NotificationPainted{c.n->id, in_room(c.card)});
        cairo_restore(cr);
        return;
    }

    // THE CARD PASS, clipped to the room.
    cairo_rectangle(cr, room.x, room.y, room.w, room.h);
    cairo_clip(cr);
    for (const LaidCard& c : laid) {
        const AppState::Notification& n = *c.n;
        const GuiRect& card = c.card;
        const double text_x    = c.text_x;
        const double text_room = c.text_room;
        const std::vector<text_shape::SharedRun>& lines = c.lines;
        // THE PUBLICATION IS THE LAYOUT'S, written for every laid card on
        // every run whatever the clip (the hit, the stack's damage owner and
        // the floaters' hulls read it).
        const GuiRect shown = in_room(card);
        st.painted.push_back(AppState::NotificationPainted{n.id, shown});
        st.painted_rect = st.painted.size() == 1
                              ? shown : union_rect(st.painted_rect, shown);
        // A CARD THE RUN'S CLIP MISSES IS NOT PAINTED (send BB, 2026-11-19;
        // clip_meets): every pixel of it — the box, the glyph in its box on
        // the padding, the lines in the text's room, all inside `card` —
        // would be clipped out, so skipping the calls writes the same bytes
        // (a standing card under a FOLLOW frame's lanes-only rects).
        if (!clip_meets(cr, card)) continue;
        // THE CLASS'S LOOK (notifications.h's THE LOOK): a standing card
        // Arc's destructive red under the print on a lit face, a normal card
        // notifyd's grey under the sidebar text; the fill at its carried
        // opacity, the glyph and the words one tone.
        const bool standing = n.cls == AppState::NotificationClass::Critical;
        const GuiPalette& p = palette();
        const GuiColor ink  = standing ? p.card_text : p.sidebar_text;
        const uint32_t fill_word = standing
                                       ? installed_theme().tones.card_standing_fill
                                       : installed_theme().tones.card_fill;
        paint_floating_box(cr, card, radius,
                           standing ? p.card_standing_fill : p.card_fill,
                           tone_alpha(fill_word));
        {
            // THE CLASS GLYPH, the drawing's whole canvas in the 16 W box at
            // the left pad, the box's top glyph_top down (icons::ink_in_box:
            // the ink's size and its offset in the box).
            const icons::Icon glyph = standing
                                          ? icons::Icon::DialogError
                                          : icons::Icon::DialogInformation;
            const icons::InkInBox fit = icons::ink_in_box(glyph, glyph_box);
            if (fit.ink_px > 0)
                icons::draw_glyph(
                    cr, glyph,
                    static_cast<double>(card.x + pad) + fit.left,
                    static_cast<double>(card.y + glyph_top) + fit.top,
                    fit.ink_px, argb32_opaque_word(ink));
        }

        if (text_room > 0.0) {
            // THE TEXT IS CUT TO ITS ROOM ON ITS OWN MASK
            // (text_shape::show_shaped_run_within: the pen and the room's
            // end are real x, and a line too long for the room is cut there
            // at exact coverage with no cairo clip).
            const text_raster::XSpan room_span{text_x, text_x + text_room};
            set_palette_source(cr, ink);
            // THE FIRST BASELINE its line box's ascent under the text's top
            // (+26 W on a one-line card, centered on the glyph's box; +25 W
            // on a wrapped one; the line-box rule), each further line one
            // line box lower (the pitch the card grows by).
            const double base = static_cast<double>(
                card.y +
                notification_text_top_px(static_cast<int>(lines.size())) +
                ascent);
            for (size_t li = 0; li < lines.size(); ++li) {
                text_shape::show_shaped_run_within(
                    cr, *lines[li], text_x,
                    base + static_cast<double>(li) * line_px,
                    &room_span, 1);
            }
        }
    }
    cairo_restore(cr);
}

// (THE DROPDOWN'S PAINTER — paint_dropdown, Windows 95's popup menu hanging
// from its anchor, its lit row, its access-key underlines, its separators and
// the True Colors row's menu check mark — RETIRED 2026-10-21 with the Settings
// popup, its last menu: the settings page lists the keys.)

// -- THE RULER LANE (top lane 5) ---------------------------------------------
//
// A LOOK/MODEL SPLIT, and it is deliberate: the ruler takes COOL EDIT'S LOOK
// and REAPER'S GEOMETRY MODEL.
//   LOOK, Cool Edit's FLIPPED RULER IN LIVE'S TONES (architect 2026-10-09,
//     the program is Cool Edit; flat since 2026-11-01 night; render.h's well
//     paragraph and its ruler block): flat `ruler_face` under the trim bar's
//     frame, no line on its top, the ticks `ruler_tick` one W wide, each
//     centered on its own exact time's place (architect 2026-11-14, send J;
//     the line rule at its exact place), STANDING ON ITS FOOT — majors 2
//     W, minors 1 W (4 and 2 device rows on the tablet), overlaying the
//     ruler's bottom pad — and the digits Bold Small 10 W in `ruler_text`,
//     bare, on the line-box baseline; then the well's line. (Kdenlive's
//     look — the majors rising above the flags, the etched ticks — stood
//     2026-08-01 to 2026-10-09; Cool Edit's E0E0E0 on the dark ground until
//     the flat well; git history.)
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
// its labels at least 8x that apart. 14 W, A WHOLE W BY HIS RULING OF
// 2026-11-05 ("round each up"): the 9 Windows px of 2026-10-02 (the laptop
// pixel's 12 re-authored) times 1.5 at the rebase to 200 % the same day gave
// 13.5 W, which stood exact for a few hours as a gesture threshold (a double
// compare against gui_scale_factor(), not a painted length), and was then
// rounded UP with the other two (kHoverSlopW, render.h;
// kNavZoomPxPerLevel, app_state.h) so that every authored number is a whole
// W. The one-device-px consequence: the minor pitch's floor is 28 device px
// on the tablet (27 before), so a zoom whose minor pitch falls in [27, 28)
// now takes the next coarser rung.
constexpr double kRulerMinMinorPitchPx = 14.0;

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

// THE COMB — the ladder's step on a displayed basis and the one expression
// that places a tick, the ruler's walk's (paint_ruler_row; the canvas draws
// no vertical grid on them, render.h's row-6 canvas paragraph, 2026-10-09).
// `vp_ms` and `ms_per_px` the basis in milliseconds (the ladder's and the
// walk's bounds), `vp_start`, `spp`, `origin` and `sr` the same basis in
// frames (the ticks' placement), `wave_w` the columns the walk spans;
// `valid` false on a basis with no span.
struct RulerComb {
    bool    valid     = false;
    double  ms_per_px = 0.0;
    double  vp_ms     = 0.0;
    double  vp_start  = 0.0;
    double  spp       = 0.0;
    int64_t origin    = 0;
    int     sr        = 0;
    int64_t step      = 0;
    int     wave_w    = 0;
    // EVERY TICK AT ITS OWN EXACT TIME (architect 2026-11-14, send J: "I
    // would expect snapping nothing to integers and leaving them rounding to
    // yield decent results"): step k's major at k · step ms and its minors
    // at (k + i / 8) · step, i = 1 .. 7 (kRulerMinorsPerStep) — the time in
    // the displayed domain's frames, · sr / 1000, PLACED AS EVERY FRAME IS
    // (architect 2026-11-06, session 68), on the basis's lattice: its EXACT
    // PLACE through displayed_x_at — where the tick is painted, the place
    // its line's own span is gated on, and a major's label's center (no
    // column is read, architect 2026-11-15, the no-grid arc). Exact
    // places are a function of the time and the lattice alone, so under a
    // pan the whole comb translates rigidly with the plate by itself, and
    // under a zoom every tick stays on its time. (i / 8 and the product by
    // the step are exact in doubles; the major's time is k · step itself.)
    double tick_frames(int64_t k, int i) const {
        const double t_ms =
            (static_cast<double>(k) +
             static_cast<double>(i) / kRulerMinorsPerStep) *
            static_cast<double>(step);
        return t_ms * static_cast<double>(sr) / 1000.0;
    }
    double tick_x(int64_t k, int i) const {
        return displayed_x_at(tick_frames(k, i), vp_start, spp, origin);
    }
    // The walk's first step, whose major stands at or left of column 0.
    int64_t first_step() const {
        return static_cast<int64_t>(std::floor(vp_ms / step));
    }
    double end_ms() const { return vp_ms + ms_per_px * wave_w; }
};
RulerComb ruler_comb(double spp, double vp_start, int64_t origin, int sr,
                     int wave_w) {
    RulerComb c;
    if (spp <= 0.0 || sr <= 0 || wave_w <= 0) return c;
    c.ms_per_px = spp * 1000.0 / static_cast<double>(sr);
    c.vp_ms     = vp_start * 1000.0 / static_cast<double>(sr);
    c.vp_start  = vp_start;
    c.spp       = spp;
    c.origin    = origin;
    c.sr        = sr;
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

// -- THE WELL'S LINES ---------------------------------------------------------
//
// THE WELL'S LINES' ONE PAINTER (render.h's THE WELL'S EDGES, the rule and
// its rows; the tone architect 2026-11-17, session 140, D2; edge to edge
// since 2026-11-19, send Z): every line inside the rows [y0, y1) — the line
// under the top icon row (the trim lane's first row), the line under the
// trough (the trim lane's last), the line under the ruler (the ruler lane's
// last), the lane's foot line and the well's foot, each across the panel's
// whole width (well_columns) — all flat `well_border`, whole cells. (The
// frame's two side lines retired with the frame.) Each painter that owns
// rows of the well calls it over its own rows after its fills (paint_trim,
// paint_ruler_row, paint_canvas_column_frame; on_redraw over the upper
// lanes on a frame with no audio), so a line's cells in a lane's rows are
// repainted wherever that lane's damage reaches. The caller's clip holds.
namespace {
void paint_well_frame_rows(cairo_t* cr, const AppState& app, int y0, int y1) {
    const WellColumns well = well_columns(app);
    const GuiRect trim = top_trim_row_area(app);
    const GuiRect ruler = top_ruler_row_area(app);
    const GuiRect foot_line = lane_foot_line_area(app);
    const GuiRect foot = well_foot_area(app);
    const int lw = hairline_px();
    const int top = trim.y;
    const int bot = foot.y + foot.h;
    const int lo = std::max(y0, top);
    const int hi = std::min(y1, bot);
    const int fw = well.end - well.x;
    if (fw <= 0 || hi <= lo) return;
    const GuiColor tone = palette().well_border;
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    // One part, its rows cut to the band asked.
    const auto part = [&](int x, int y, int w, int h) {
        const int ry0 = std::max(y, lo);
        const int ry1 = std::min(y + h, hi);
        if (w <= 0 || ry1 <= ry0) return;
        paint_cell_rect(cr, GuiRect{x, ry0, w, ry1 - ry0}, tone);
    };
    // THE LINES, across the panel's whole width.
    part(well.x, trim.y, fw, std::min(lw, trim.h));
    part(well.x, trim.y + trim.h - std::min(lw, trim.h), fw,
         std::min(lw, trim.h));
    part(well.x, ruler.y + ruler.h - std::min(lw, ruler.h), fw,
         std::min(lw, ruler.h));
    part(well.x, foot_line.y, fw, foot_line.h);
    part(well.x, foot.y, fw, foot.h);
    cairo_restore(cr);
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
    // old and new columns from the viewport's lanes' top row
    // (playhead_invalidate_rect, main.cpp: the head's rows are inside it)
    // or the viewport's lanes whole (the discrete moves'
    // Viewport::invalidate_waveform_area), so the frame that shows a moved
    // head is always a publishing one.
    // THE HIT IS REAL, THE COVERAGE TEST ITS HULL (architect 2026-11-15, the
    // no-grid arc): the published rect stands on the head's exact place;
    // "did this run's clip redraw it" names pixels, so it asks the rect's
    // whole pixels (render.h's hull).
    const auto publish_head = [&](const GuiRectF& head) {
        const GuiRectF last = app.playhead_head_hit;
        if (head.w > 0.0 && head.h > 0.0) {
            if (clip_covers_drawable(cr, app, hull(head)))
                app.playhead_head_hit = head;
        } else if (last.w <= 0.0 || last.h <= 0.0 ||
                   clip_covers_drawable(cr, app, hull(last))) {
            app.playhead_head_hit = GuiRectF{0.0, 0.0, 0.0, 0.0};
        }
    };
    if (lane.w <= 0 || lane.h <= 0) {
        publish_head(GuiRectF{0.0, 0.0, 0.0, 0.0});
        return;
    }

    cairo_save(cr);
    // THE LANES (architect 2026-11-01 night, the flat well; render.h's well
    // paragraph): the ruler FLAT `ruler_face` across the waveform area's
    // columns — the panel's whole width since 2026-11-19 (send Z, edge to
    // edge), so the body tone `face` laid first shows nowhere on a full
    // window; the marker lane FLAT `lane_face` across them; then THE WELL'S
    // LINE under the ruler, the ruler lane's last row (paint_well_frame_rows).
    const GuiPalette& pal = palette();
    const int ruler_h = ruler_h_px();
    const GuiRect lanes_area = waveform_area(app);
    const int well_x0 = lanes_area.x;
    const int well_x1 = lanes_area.x + lanes_area.w;
    paint_cell_rect(cr, lane, pal.face);
    if (marker.w > 0 && marker.h > 0) {
        const int in0 = std::clamp(well_x0, marker.x, marker.x + marker.w);
        const int in1 = std::clamp(well_x1, in0, marker.x + marker.w);
        if (in0 > marker.x)
            paint_cell_rect(cr, GuiRect{marker.x, marker.y, in0 - marker.x,
                                        marker.h},
                            pal.face);
        if (marker.x + marker.w > in1)
            paint_cell_rect(cr, GuiRect{in1, marker.y,
                                        marker.x + marker.w - in1, marker.h},
                            pal.face);
        if (in1 > in0)
            paint_cell_rect(cr, GuiRect{in0, marker.y, in1 - in0, marker.h},
                            palette().lane_face);
    }
    // THE FRAME over the two lanes' rows (the head of this block).
    {
        const int rows_end =
            marker.h > 0 ? marker.y + marker.h : lane.y + lane.h;
        paint_well_frame_rows(cr, app, lane.y, rows_end);
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
    // THE RULER'S COLUMNS ARE THE CANVAS'S (2026-10-09): every tick, digit
    // and the head from the waveform area's x (the panel's first column since
    // 2026-11-19, send Z).
    const GuiRect area = waveform_area(app);
    const int cx = area.x;

    // THE WALK WIDTH IS THE PLATE'S OWN, NOT THE LIVE ONE — the same
    // published-width rule the flag cache spells at its wave_w read
    // (waveform_cache.cpp). The spp above comes from the published fingerprint,
    // so the width bounding the tick walk, the label span and the head's
    // cull must be the width that spp was published AGAINST:
    // during the async resize window (on_resize stores new dimensions while the
    // OLD plate stays drawn until the worker publishes) a grown live width
    // would walk ticks across the newly exposed right-hand area at the old
    // plate's scale — ticks disagreeing with the ink they sit over. The
    // live-width fallback mirrors plate_viewport_basis's own cold arm (no plate
    // published yet, spp already live).
    const int wave_w = wf_cache.fp_area_w > 0 ? wf_cache.fp_area_w
                                              : area.w;
    const RulerComb comb = ruler_comb(basis.spp, basis.vp_start,
                                      basis.lattice_origin, sr, wave_w);
    if (!comb.valid) {
        publish_head(GuiRect{0, 0, 0, 0});
        cairo_restore(cr);
        return;
    }
    // THE RULER'S FACE (the head of this body): flat `ruler_face` across the
    // waveform area's columns over the ruler's rows, the lane's last row the
    // well's line laid above. The face reads the LIVE area, as the frame
    // does (paint_well_frame_rows); the walk below reads the plate's width.
    paint_cell_rect(cr, GuiRect{well_x0, lane.y, well_x1 - well_x0,
                                std::min(ruler_h, lane.h)},
                    palette().ruler_face);

    const int64_t step   = comb.step;
    const double  end_ms = comb.end_ms();

    // THE TICKS STAND ON THE RULER'S FOOT (the flipped ruler; render.h's
    // ruler block): each ONE CANVAS LINE wide (waveform_line_px, one W since
    // 2026-11-04, the unit), centered on its time's exact place (the line
    // rule at its exact place, render.h's waveform_line_span), in
    // `ruler_tick`, rising the major's 2 W or the minor's 1 W (4 and 2 device
    // rows on the tablet; scaled_px) from the foot into the ruler's bottom
    // pad, NO SHADOW.
    const int ruler_bottom = lane.y + std::min(ruler_h, lane.h);   // one past
    const int major_top = ruler_bottom - ruler_major_tick_h_px();
    const int minor_top = ruler_bottom - ruler_minor_tick_h_px();

    // THE DIGITS: THUNAR'S COLUMN HEADER (architect 2026-11-19, send Z;
    // DESIGN §4) — THE RULER FACE (gui_font.h's table, `Small`: BOLD 10 W,
    // Arc's `font-size: smaller`, 20 device px on the tablet) in
    // `ruler_text` (the text tone at .8 over the ruler's face, Arc's column
    // header; arc_dark B6BCC6), BARE, ON THE LINE-BOX BASELINE, +11 W of
    // the ruler's 16 (render.h's ruler_baseline_px: 22 rows on the tablet).
    const GuiFont font = gui_font(GuiFace::Small);
    const double baseline =
        static_cast<double>(lane.y + ruler_baseline_px());

    // THE COMB IS RIGID UNDER PAN BY ITSELF (architect 2026-11-14, send J;
    // the rule at RulerComb::tick_frames): every tick is painted at its own
    // time's exact place at its exact coverage, so no tick rounds against
    // the screen and a pan moves every one by the same real amount — the
    // smoothness the comb was first made rigid for (architect 2026-08-01,
    // the grab-pan shimmer: ticks rounded one by one stepped at different
    // moments, "the ticks are purely informative — I'd rather have
    // smoothness than total precision") with every tick on its exact time.
    const int64_t first_step = comb.first_step();
    // THE LABELS ARE CLIPPED AT BOTH ENDS (architect 2026-10-22, "clip"): a
    // label is painted CENTERED on its major's exact place wherever it
    // stands and
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
    // THE TICK'S WIDTH IS THE CANVAS'S LINE, ONE W (hairline_px, the line
    // rule; architect 2026-11-04, the unit, reversing the one device px of
    // 2026-11-01 night, M5b), CENTERED ON ITS TIME'S EXACT PLACE (architect
    // 2026-11-14, send J; waveform_line_span).
    const int t = hairline_px();
    // ONE MAJOR'S LABEL at its major's exact place `mark_x` (device px
    // from the waveform's first column, RulerComb::tick_x) and its step
    // time `step_ms`, CENTERED ON THE MAJOR'S EXACT PLACE (architect
    // 2026-11-15, the no-grid arc: the digits stand with their tick — the
    // glide's trial of 2026-11-14, send J, is the rule): the run's pen is
    // the place less half the run's own width, a real x, no half rounded
    // and no column asked, so the number is centered on the tick painted
    // at that place and moves with it by the same real amount;
    // text_shape's placement is untouched. The label's TIME is the exact
    // step time.
    const auto paint_major_label = [&](double mark_x, double step_ms) {
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
        // these bytes — handed to the same show_shaped_run call below.
        const std::string txt = ruler_label_text(label_ms, step);
        const text_shape::SharedRun handle = text_shape::memoized_run(font, txt);
        const text_shape::ShapedRun& run = *handle;
        // EVERY LABEL IS ONE COLOR (architect 2026-10-02, "give the same
        // colour to all the numbers"): `ruler_text`, bare (the block above).
        // The walk's clip cuts it at the canvas's columns.
        const double x =
            static_cast<double>(cx) + mark_x - run.width_px / 2.0;
        set_palette_source(cr, palette().ruler_text);
        text_shape::show_shaped_run(cr, run, x, baseline);
    };
    // THE WALK'S CLIP: the waveform's columns [0, wave_w) over the ruler's
    // rows, so a tick past the last column carries nothing beside wave_w.
    cairo_save(cr);
    cairo_rectangle(cr, cx, lane.y, wave_w, ruler_bottom - lane.y);
    cairo_clip(cr);
    // THE WALK'S STEPS: from first_step through ONE STEP PAST THE END (the
    // clip rule above): the major right of the last column is walked for
    // its label, its tick drawn only where its line reaches the columns.
    const auto step_walked = [&](int64_t k) {
        return static_cast<double>(k) * static_cast<double>(step) <=
               end_ms + static_cast<double>(step);
    };
    // THE LABELS, EVERY WALKED MAJOR'S, BEFORE ANY TICK: a label paints
    // whether or not its major's tick is on the lane (the walk's start
    // above), the clip cutting it, and the ticks then stand over whatever
    // of a label's ink could reach their rows (the pad keeps them apart
    // today: on the tablet the digits' baseline stands 12 rows over the
    // foot, the major rises 8). Cairo's pass whole, ahead of the stamp's (its contract: no
    // cairo drawing between its shapes).
    for (int64_t k = first_step; step_walked(k); ++k)
        paint_major_label(
            comb.tick_x(k, 0),
            static_cast<double>(k) * static_cast<double>(step));
    // THE TICKS, THROUGH THE COVERAGE STAMP (architect 2026-11-14, send J:
    // stamped directly at exact coverage, no cairo path per tick — the
    // ruling at CoverageStampPass, cool_edit_paint.h): each tick
    // one rect with real column edges — the one-W line centered on its own
    // exact place, DRAWN WHEREVER ANY OF IT IS INSIDE THE LANE'S COLUMNS and
    // cut at both edges (waveform_line_span, whose emptiness is the only
    // test there is; architect 2026-11-15, the no-grid arc) — on its whole
    // rows from its top to the ruler's foot, in `ruler_tick` over the
    // ruler's face.
    {
        const GuiColor ink = palette().ruler_tick;
        const double foot = static_cast<double>(ruler_bottom);
        CoverageStampPass ticks;
        ticks.begin(cr);
        for (int64_t k = first_step; step_walked(k); ++k) {
            for (int i = 0; i < kRulerMinorsPerStep; ++i) {
                const WaveformLineSpan span =
                    waveform_line_span(cx, wave_w, comb.tick_x(k, i), t);
                if (span.empty()) continue;
                ticks.rect(span.x0,
                           static_cast<double>(i == 0 ? major_top
                                                      : minor_top),
                           span.x1, foot, ink);
            }
        }
        ticks.end();
    }
    cairo_restore(cr);

    // -- THE PLAYHEAD'S HEAD ---------------------------------------------------
    //
    // THE HEAD IS COOL EDIT'S CURSOR TRIANGLE IN THE RULER (architect
    // 2026-10-09; render.h's playhead paragraph) — THE SAME DRAWING AS THE
    // MARKER'S CHIP (architect 2026-11-04, mock k1, "4 accept overlap"): A
    // SMOOTH-EDGED TRIANGLE WITH NO OUTLINE (architect 2026-11-19, send AA:
    // the chips' outline retired, the head's with it; the bare triangle of
    // the nominal 4 W rows, cue_chip_h_px, 8 device px on the tablet),
    // AT THE PLAYHEAD'S EXACT x, ITS APEX ON THE PLAYHEAD LINE'S CENTER (architect
    // 2026-11-14 night, send J: "I think the chips should also go
    // anti-aliased", "Agree on both."; cool_edit_paint.h's THE CUE'S CHIP
    // owns the drawing, render.h's cue block its lengths) — its fill the
    // palette's `playhead_head` (Live's cyan 03C3D5 since 2026-11-01), NO
    // SHADOW (2026-11-02), the head's own tone apart from the line's static
    // `playhead_line` (render_playhead), THE TRIANGLE ON THE RULER'S LAST
    // cue_chip_h_px DEVICE ROWS (200 %: rows 192 .. 199), its apex on THE
    // BOTTOM EDGE OF THE RULER'S FOOT ROW, over the ticks' feet (2026-10-09
    // ~21:00; the ruler's foot since the flat well). It stands over the
    // ticks and digits the walk above laid down, covering whatever lies
    // under its whole-covered pixels and blending its edge over them — ITS
    // TOP REACHES INTO THE DIGITS where it stands under a label, ACCEPTED
    // (architect 2026-11-04: "timestamp not really important for fine tune,
    // just ballpark"): no clipping, no avoidance, as a chip paints over a
    // label. NO SNAP, NO AVOIDANCE: it may stand on a marker's column, the
    // cue's chip being in the marker lane below (render.h's playhead
    // paragraph). It draws NOTHING across the trim bar, the marker lane and
    // the frame's lines; its canvas line is paint_playheads'.
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
        // The line's own column (playhead_pixel_x on the plate basis and
        // its lattice origin) — the cull's, the hit's and the damage box's —
        // and its exact place (playhead_exact_x on the same), where the head
        // is painted, its apex on the line's center.
        const int col   = playhead_pixel_x(
            app, static_cast<int64_t>(basis.vp_start), basis.spp,
            basis.lattice_origin);
        const double x  = playhead_exact_x(
            app, static_cast<int64_t>(basis.vp_start), basis.spp,
            basis.lattice_origin);
        // The head's rows, the chip's height (render.h's cue_chip_h_px).
        const int rows  = cue_chip_h_px();
        // THE TWO EXTENTS (render.h's cue block): the painted reach, the
        // columns [col − reach, col + reach) the head can touch — the cull
        // here and the damage box's — and the hit extent [x − left,
        // x + right) off the head's exact place, the published hit's.
        const int reach = cue_chip_paint_reach_px();
        const int left  = cue_chip_hit_left_px();
        const int right = cue_chip_hit_right_px();
        GuiRectF head_hit{0.0, 0.0, 0.0, 0.0};
        if (col + reach > 0 && col - reach < wave_w) {
            const int head_top = ruler_bottom - rows;
            // THE HEAD'S CLIP: the waveform's columns over the head's own
            // rows, the ruler's last `rows` — whole pixels, the stamp's clip
            // box. The apex on the foot row's bottom edge, `ruler_bottom`.
            cairo_save(cr);
            cairo_rectangle(cr, cx, head_top, wave_w, rows);
            cairo_clip(cr);
            paint_ce_cue_chip(cr, static_cast<double>(cx) + x,
                              static_cast<double>(ruler_bottom),
                              palette().playhead_head);
            cairo_restore(cr);
            // THE HEAD'S HIT (architect 2026-10-09 ~17:40, the head drag;
            // point_on_playhead_head, input_pointer.cpp): THE HEAD AT ITS
            // EXACT PLACE (architect 2026-11-15, the no-grid arc: "yes, I'd
            // like to move to fractional hitboxes.") — the real interval
            // [x − left, x + right) cut to the waveform's columns as the
            // clip above cuts them, over the head's `rows` device rows of
            // the ruler's (render.h's THE CHIP'S TWO EXTENTS, the cue
            // chip's own box), the painted head within a px of it
            // and nothing beyond ("I don't think we should get in the habit
            // of making invisible hitboxes"), published by the pass's rule
            // (publish_head, the pass's head); none where the cut leaves
            // nothing (a head showing only its painted reach's last pixels
            // at an edge).
            const double x0 = std::max(0.0, x - static_cast<double>(left));
            const double x1 = std::min(static_cast<double>(wave_w),
                                       x + static_cast<double>(right));
            if (x1 > x0)
                head_hit = GuiRectF{static_cast<double>(cx) + x0,
                                    static_cast<double>(head_top), x1 - x0,
                                    static_cast<double>(rows)};
        }
        publish_head(head_hit);
    }

    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_waveform_plate -------------------------------

void GuiPaintHandler::paint_waveform_plate(GuiCanvasSink& sink,
                                           const GuiRect& area) {
    // The live plate (wf_cache.columns) is produced by one of two paths,
    // both of which leave this paint path draw-only:
    //   1. Worker full render — maybe_enqueue_waveform_render
    //      dispatches a full-window render on GuiWaveformWorker,
    //      which swaps into wf_cache.columns on completion. Fires
    //      for UNDRIVEN changes — resize, the launch load — and as the
    //      on_tick backstop for any residual fingerprint drift (a
    //      warp_frame_map hash included). Map EDITS themselves are
    //      user-driven and take path 2, and so does FOLLOW'S CAMERA since
    //      2026-09-02 — its page-in and its scroll's frames (the playhead
    //      line is drawn onto that very frame, so the move cannot wait a
    //      publish — Viewport::follow_scroll_if_needed, follow_scanner).
    //   2. Synchronous full render — force_synchronous_waveform_rebuild
    //      renders the full window inline on the GUI thread for every
    //      USER-DRIVEN viewport change: zoom, center-on-playhead, the
    //      one-shot jumps, and panning/scrolling (which had its own
    //      incremental shift-and-strip path until 2026-07-26 — retired so
    //      a moving plate and a resting one come off one route).
    // The paint path is draw-only — it records whatever plate the live
    // slot currently holds. For worker-path renders that may
    // be a one- or two-frame-old viewport during the worker-rebuild
    // window; the synchronous path updates the plate in the same
    // frame, so it has no such lag. The flag layer closes
    // any mismatch by layering flags keyed off the same displayed-viewport.
    //
    // If no plate is rendered yet (initial load, before the first
    // worker completion), the sink records nothing and the canvas
    // ground fill shows through. The user-visible difference is one
    // extra paint frame of empty canvas between load and first
    // waveform display, masked by the open's PRESENTATION HOLD (platform.h's
    // head; main.cpp's initial-load / release block), which keeps the
    // empty-waveform frame on screen until live_plate_published releases it
    // with the first complete picture.
    //
    // DRAW-ONLY HERE: this call records the
    // plate as the renderer produced it, drawn once over the plain
    // `waveform_canvas` ground render_canvas laid — the plate's transparent
    // gaps show that ground, the canvas having that one painter. No pass
    // recolours the waveform at rest (the out-of-trim dim retired
    // 2026-07-26, the trim bar the window's picture); WHILE A SWEEP'S
    // POINTER IS DOWN the sweep highlight blends its span toward white over
    // the plate and every line after it (paint_sweep_highlight, the list's
    // last record, architect 2026-11-12).
    //
    // The clip is the AREA, the canvas's interior (2026-10-09: the canvas's
    // frame stands outside it), so a plate published at a stale size never
    // paints over the frame.
    //
    // THROUGH THE CANVAS SINK (2026-10-13, canvas_sink.h): the list takes
    // the live COLUMN BOUNDS (WaveformCache::columns) and the published
    // inks, the GPU rasterizing the antialiased plate from them
    // (GuiCanvasSink::plate; kListFragmentShader, gl_canvas.cpp).
    sink.plate(area, wf_cache.columns, wf_cache.fp_inks);
}

// -- GuiPaintHandler::plate_viewport_basis ------------------------------

// See the declaration comment in paint_handler.h: the fp-recipe basis locked to
// the blitted plate while the worker rebuilds, with the live spp fallback when
// no plate has published a span yet.
GuiPaintHandler::PlateViewportBasis
GuiPaintHandler::plate_viewport_basis() const {
    PlateViewportBasis b;
    b.spp = wf_cache.fp_area_w > 0
        ? recovered_painter_spp(wf_cache.fp_vp_end - wf_cache.fp_vp_start,
                                wf_cache.fp_area_w)
        : painter_samples_per_pixel(app, audio, waveform_area(app));
    b.vp_start = static_cast<double>(wf_cache.fp_vp_start);
    b.lattice_origin = wf_cache.fp_lattice_origin;
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
// tempo, up to ~8 at the slope ceiling of 16, which a resolved
// label-reference segment shares, and NOT the "sample or two" this comment
// claimed for one day. THIS SITE ADDS THE THIRD TERM: the nearbyint(T)
// below is up to another half OUTPUT sample of painted width. So the band can
// come out that much wider than N/2 (the worked case — tempo 0.30, reset
// at source 307 — paints 2049 where N/2 is 2048). NOT CLAMPED to N/2: the
// clamp would hide the engine's geometry rather than fix it, and this band
// exists to depict that geometry. It reaches no audio and it is SUB-PIXEL at
// the working zoom for every resolved slope; it is NOT the trim crop's rounding
// class (that comparison is withdrawn — the trim's rounding is transient,
// while this seam sits in every deliverable's geometry).
//
// DAMAGE. The right edge depends on the reset's frame and on the displayed
// map, both of which the left edge already depended on, so no new damage
// owner is needed: a nudge and a drag commit pay the FULL waveform-area
// invalidate (position_nudge.cpp's tail, marker_drag.cpp's commit), every
// map change (a cent step, a warp move) runs the full waveform damage
// beside its plate re-warp (warpmarkers_ops.cpp), and the drag's every
// motion event rebuilds the flag cache in the event, whose tail damages the
// ring's four sides where it stood and where it stands
// (paint_phase_reset_overlay_ring's DAMAGE). A hop snap under any of them
// rides that damage.
//
// THE GEOMETRY AND VISIBILITY OWNER, kept SEPARATE from its one consumer
// (paint_phase_reset_overlay_ring) rather than folded into it. It carries every
// visibility gate — view, focus, the multi-select suppression, the eligible-marker
// resolve, the sub-pixel and offscreen refusals — plus the clipped span, and
// Selection::phase_overlay_subject MIRRORS its selection-state gates — MINUS the
// geometry ones, which are not selection state — to decide whether a restore's
// sanitize changed the subject and, since 2026-07-28, whether Space auditions the
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
    // selection shows it as before; the multi-select builders all publish,
    // so the overlay's appear/disappear rides their flag rebuild's ring
    // damage, paint_phase_reset_overlay_ring's DAMAGE.)

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
        // labels).
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

    // BOTH EDGES ARE MAPPED, EACH FROM ITS OWN VALUE — the left from the
    // marker's paint sample, which is the stem renderer's own placement, the
    // right from the grain end, which is the lattice point the header
    // derives — so the far edge sits on screen where its lattice point sits
    // in samples: it stands still while the reset is nudged inside one hop
    // cell and snaps a whole hop at the boundary, which is what the header's
    // "STANDS STILL" sentence promises. The ring is painted, and gated, at
    // the two values' exact places (displayed_x_at, below; architect
    // 2026-11-14, send J; the gates 2026-11-15).
    const double right_ms = ms + static_cast<double>(width_samples);
    // THE EXACT PLACES the ring is painted at (displayed_x_at,
    // warp_frame_map_view.h) — AND ITS GATES' (architect 2026-11-15, the
    // no-grid arc: the ring shows iff its real span meets the area; no
    // column is asked).
    const double left_x =
        displayed_x_at(ms, vp_start, spp, basis.lattice_origin);
    const double right_x =
        displayed_x_at(right_ms, vp_start, spp, basis.lattice_origin);

    // Too-zoomed-out: an extent under one px wide paints nothing at all: no
    // sliver, no clamped minimum.
    if (right_x - left_x < 1.0) return out;

    // THE RING'S REAL SPAN is [left_x − t / 2, right_x) — its left side the
    // line centered on the reset's place, its right side flush inside the
    // grain's end (paint_phase_reset_overlay_ring) — and the band shows
    // whenever any of it lies inside the area's columns [0, area.w), cut at
    // the edges, even where the reset's own place is off-screen left (the
    // tail can be visible while the left side is not).
    const double half_t = static_cast<double>(waveform_line_px()) / 2.0;
    if (!(right_x > 0.0) ||
        !(left_x - half_t < static_cast<double>(area.w)))
        return out;

    out.valid    = true;
    out.left_x   = left_x;
    out.right_x  = right_x;
    return out;
}

// THE OVERLAY RING — the phase-reset overlay's WHOLE visual (architect
// 2026-07-27): the band's border and nothing else, no fill, so the band
// READS as the edges of a span rather than as a tinted region, painted
// AFTER the plate and over the ink.
//
// THE RING IS FOUR SOLID ONE-W LINES IN THE `cue_outline` (architect
// 2026-11-04: the dotted stems at rest retired, and the ring with them —
// the ring and the cue one unit, 2026-09-17, so it wears the chip's dark
// line, 111111), WHATEVER THE SELECTION, a coincident reset's too (its
// struck label the mark, architect 2026-10-26),
// EVERY SIDE AT ITS EXACT PLACE (architect 2026-11-14, send J: "the
// anti-aliasing I would like to commit to that as well, the full
// anti-aliasing"; the rule at render.h's waveform_line_span), XL the
// reset's own exact place and XR the grain end's CUT TO THE AREA'S RIGHT
// EDGE, both in window px (the band's left_x and right_x off the area's x):
//   THE LEFT SIDE the line centered on the reset's frame, [XL − t / 2,
//     XL + t / 2] (waveform_line_px, one W), drawn wherever any of it is
//     inside the area and cut to the area's columns (waveform_line_span,
//     the line's one edge rule), so a reset off the area's left edge by
//     half a W or more paints no left side; THE RIGHT SIDE
//     [XR − t, XR], flush inside the band, so a band cut at the area's
//     right edge draws it there, flush, as the trim bar's clipped span
//     reads. BOTH ON THE ROWS BETWEEN THE RUNS — the canvas's rows less its
//     first and last t, the stereo divider's row crossed (architect
//     2026-11-02) — so no cell of a corner is blended twice.
//   THE TOP RUN on the canvas's first t rows and THE BOTTOM RUN on its
//     last t, from the left side's outer edge XL − t / 2 (cut to the area's
//     left edge) through XR, so the corners close at every t.
// THE LEFT SIDE YIELDS TO ITS RESET'S SELECTION STEM (THE COINCIDENT LINES,
// paint_playheads' paragraph the rule; stem_stands_at): where a published
// stem stands on the reset's exact x the stem — a later record of the same
// width and color over the same rows — is the left side, and this one is
// not drawn, so no cell of the two is blended twice; the runs' ends under
// that stem, on the canvas's first and last t rows, are. ON A BAND UNDER
// 1.5 t WIDE the right side reaches back over the left side's interval and
// the cells both cover are blended twice too — one color, a fringe a
// little darker than its coverage; left as it falls (lines at different
// places blend in list order).
// THE PALETTE'S WORD AT EXACT COVERAGE: blended rects at 100 percent
// (GuiCanvasSink::blended_rect), the word itself on every cell a side
// covers whole — at the tablet's even t whole places are whole cells — and
// the word over what lies beneath by the cell's coverage where an edge
// crosses it. No other
// reader takes the ring's cells: it is no hit target.
// DAMAGE (architect 2026-11-16, the beats): this pass paints live at every
// list build from app state, never from a cached surface, and every change
// to its inputs misses the flag cache's fingerprint, WHOSE REBUILD'S TAIL
// DAMAGES THE RING (maybe_rebuild_flag_cache, waveform_cache.cpp): the four
// sides where the last rebuild left them and the four where they now stand,
// each side's own cells (phase_reset_ring_sides and phase_reset_ring_damage
// below), where the miss is the marker-driven content alone — a marker
// drag's motion, a nudge, a selection change — and the whole waveform area
// with every other field; a restore's sanitize that changed the subject
// adds the whole area itself (Selection::damage_overlay_on_subject_change,
// the one selection write that runs no rebuild) and a palette install the
// whole window. THE GEOMETRY THIS PASS DRAWS IS NOTED AGAINST THE ONE THAT
// REBUILD LAST COMPARED (note_ring_drawn, FlagCache's `ring_declared`): a
// frame that draws another — served between a write and the tick's
// rebuild, or promoting a changed item map — has put a geometry on part of
// the canvas no damage was declared for, and the next rebuild declares the
// whole area. A disabled reset paints no ring (the band's gate).
// THROUGH THE CANVAS SINK (2026-10-13, canvas_sink.h): four records at most.
void GuiPaintHandler::paint_phase_reset_overlay_ring(
    GuiCanvasSink& sink, const GuiRect& area) {
    const PhaseResetOverlayBand band = phase_reset_overlay_band(area);
    note_ring_drawn(phase_reset_ring_sides(band, area));
    if (!band.valid) return;

    const int      t    = waveform_line_px();
    const GuiColor ink  = palette().cue_outline;
    const double   ax0  = static_cast<double>(area.x);
    const double   ax1  = static_cast<double>(area.x + area.w);
    const double   xl   = ax0 + band.left_x;
    const double   xr   = std::min(ax0 + band.right_x, ax1);
    // The canvas's rows: the runs' t at its head and foot, the sides between.
    const double y0 = static_cast<double>(area.y);
    const double y1 = static_cast<double>(area.y + area.h);
    const double ya = y0 + static_cast<double>(t);
    const double yb = y1 - static_cast<double>(t);
    // THE RUNS, from the left side's outer edge through the right side's.
    const double run_lo =
        std::max(xl - static_cast<double>(t) / 2.0, ax0);
    sink.blended_rect(run_lo, y0, xr, ya, ink, 100);
    sink.blended_rect(run_lo, yb, xr, y1, ink, 100);
    // THE LEFT SIDE, the line's own span (drawn where any of it is inside,
    // cut at the edges) — unless its reset's stem stands on it (the
    // coincident lines).
    if (!stem_stands_at(area, band.left_x)) {
        const WaveformLineSpan left =
            waveform_line_span(area.x, area.w, band.left_x, t);
        sink.blended_rect(left.x0, ya, left.x1, yb, ink, 100);
    }
    // THE RIGHT SIDE, flush inside the band.
    sink.blended_rect(std::max(xr - static_cast<double>(t), ax0), ya, xr, yb,
                      ink, 100);
}

// THE RING'S FOUR RECORDS AS REAL RECTS, FOR ITS DAMAGE (architect
// 2026-11-16, the beats; GuiPhaseResetRingSides, paint_handler.h): the top
// run, the bottom run, the left side and the right side of `band` over
// `area`, each the real edges the painter above hands the sink. IN LOCKSTEP
// WITH paint_phase_reset_overlay_ring: every expression here is that
// body's own, restated (the painter is not routed through this — its
// arithmetic is the picture and stays where it is read), so a change to
// either is a change to both. The left side is given whether or not the
// painter draws it (the type's note). No band, no sides.
GuiPhaseResetRingSides GuiPaintHandler::phase_reset_ring_sides(
    const PhaseResetOverlayBand& band, const GuiRect& area) {
    GuiPhaseResetRingSides out;
    if (!band.valid) return out;
    const int    t   = waveform_line_px();
    const double ax0 = static_cast<double>(area.x);
    const double ax1 = static_cast<double>(area.x + area.w);
    const double xl  = ax0 + band.left_x;
    const double xr  = std::min(ax0 + band.right_x, ax1);
    const double y0  = static_cast<double>(area.y);
    const double y1  = static_cast<double>(area.y + area.h);
    const double ya  = y0 + static_cast<double>(t);
    const double yb  = y1 - static_cast<double>(t);
    const double run_lo =
        std::max(xl - static_cast<double>(t) / 2.0, ax0);
    const WaveformLineSpan left =
        waveform_line_span(area.x, area.w, band.left_x, t);
    out.valid   = true;
    out.side[0] = {run_lo, y0, xr, ya};
    out.side[1] = {run_lo, yb, xr, y1};
    out.side[2] = {left.x0, ya, left.x1, yb};
    out.side[3] = {std::max(xr - static_cast<double>(t), ax0), ya, xr, yb};
    return out;
}

// THE RING'S DAMAGE FOR A CHANGE OF GEOMETRY (architect 2026-11-16, the
// beats): the cells of the four sides `before` and of the four `after`,
// EACH SIDE ITS OWN RECT — the runs are wide and two rows thin, the sides
// thin and the canvas tall, so their bounding box would be the band's whole
// face — a side's cells the floor of its real edges through their ceiling,
// the very cells its record covers (GuiCanvasSink::blended_rect), so each
// rect holds its side whole. A side's two rects are declared as one where
// they overlap or abut (both stand on the same rows, so their bound is
// their union: a run that grew, a side that moved under a line's width)
// and apart otherwise (the right side after a hop snap). A geometry with no
// band gives none; an empty side (the left side off the area's edge, the
// sink's own refusal) gives none.
void GuiPaintHandler::phase_reset_ring_damage(
    const GuiPhaseResetRingSides& before, const GuiPhaseResetRingSides& after,
    std::vector<GuiRect>& out) {
    const auto cells = [](const GuiPhaseResetRingSides& g, std::size_t k) {
        const GuiPhaseResetRingSides::Edges& e = g.side[k];
        if (!g.valid || e.x1 <= e.x0 || e.y1 <= e.y0)
            return GuiRect{0, 0, 0, 0};
        const int x0 = static_cast<int>(std::floor(e.x0));
        const int y0 = static_cast<int>(std::floor(e.y0));
        return GuiRect{x0, y0, static_cast<int>(std::ceil(e.x1)) - x0,
                       static_cast<int>(std::ceil(e.y1)) - y0};
    };
    for (std::size_t k = 0; k < before.side.size(); ++k) {
        const GuiRect a = cells(before, k);
        const GuiRect b = cells(after, k);
        const bool has_a = a.w > 0 && a.h > 0;
        const bool has_b = b.w > 0 && b.h > 0;
        if (has_a && has_b &&
            std::max(a.x, b.x) <= std::min(a.x + a.w, b.x + b.w) &&
            std::max(a.y, b.y) <= std::min(a.y + a.h, b.y + b.h)) {
            out.push_back(union_rect(a, b));
            continue;
        }
        if (has_a) out.push_back(a);
        if (has_b) out.push_back(b);
    }
}

// -- GuiPaintHandler::paint_trim -----------------------------------------

// The LIVE trim pass: every trim pixel — the lane's ground, the well's
// frame's share of it, the trough and the pill (render_trim_flags) — paints
// here per frame, entirely inside its lane,
// which no later pass paints on; its slot is in the paint-order block in
// on_redraw (the one authoritative sequence).
//
// BASIS: the FREE item-geometry owners — item_viewport_basis(app, audio)
// and displayed_or_live_target_map(app, audio) — feeding the shared geometry
// owners displayed_trim_ms / trim_bound_column / trim_bridge_gap /
// trim_endcap_rect inside the renderer — the chain's ONE run: the trim hits
// (hit_test_trim_endcap / point_in_trim_bridge_span, both reached through
// route_trim_bar_press) read what this pass PUBLISHES
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
    // No trim gate: the window is ALWAYS set (2026-07-30), so the bar always
    // paints — at the full window the pill spans the whole lane between the
    // song's edges. Its caps are a state's, not the bar's: at rest the bare
    // pill, the caps only while a part is hovered or held or a TRIM sweep
    // moves an end (trim_bar_tones' table, render.h).
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
    // write damages at least this lane whole — the sweep's and the bar
    // drags' per-motion writes the lane itself (top_trim_row_area;
    // architect 2026-11-16, the beats), commit_trim_mutation, the maximizer
    // and the tab, load and load-in-place roads the viewport's lanes, the
    // band's foot through the waveform's bottom at the window's width —
    // so the frame that
    // shows a moved bound is always a publishing one. The early returns below
    // publish COLD when they may publish at all: a lane with no bar has
    // nothing to grab.
    // THE TRIM LANE'S OWN GROUND (the palette's `face`, the theme's body
    // tone `body_face`), across the panel's whole width, and over it THE
    // WELL'S LINES' share of the lane — its top line on the lane's first
    // row, its line under the trough on the last (paint_well_frame_rows;
    // architect 2026-11-17, session 140; edge to edge since 2026-11-19);
    // the trough and the pill render_trim_flags', on the waveform area's
    // columns. The lines paint before the early returns below, so they stand
    // whatever the bar does.
    const GuiRect trim_row = top_trim_row_area(app);
    if (trim_row.w > 0 && trim_row.h > 0) {
        paint_cell_rect(cr, trim_row, palette().face);
        paint_well_frame_rows(cr, app, trim_row.y, trim_row.y + trim_row.h);
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
    // long press frames that window in here exactly as it does outside.
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
    // window's WHOLE RESTING display — the sweep's transient canvas
    // highlight the one exception (paint_sweep_highlight, 2026-11-12).
    // THE BAR'S STATE (architect 2026-11-19 / 2026-11-20, send CC): the
    // part under the hovering pen, the part the bar's own press holds and
    // the end a TRIM sweep that wrote is moving (send DD, 2026-11-20), read
    // off their records (trim_bar_look,
    // app_state.h, every edge's damage there); the tones and the caps are
    // the one table's (trim_bar_tones, render.h). THE LOOK IS ASKED AFTER
    // THIS PASS PUBLISHES THE STASH (render_trim_flags' `look`): a pen
    // lift's pending answer is a question about the picture this pass
    // paints, so the publishing pass — and only it — resolves it on the
    // zones just published, before the pill and the caps are stamped
    // (resolve_trim_bar_lift; AppState::TrimBarHover's `lift_pending`).
    // NO PLAYHEAD IN THE BAR (architect 2026-10-09 ~14:30, "no dots on the
    // trim bar"; render.h's view-bar paragraph).
    render_trim_flags(cr, top_strip, trim_row, wave_rect,
                      basis.vp_start_frame, basis.vp_end_frame,
                      basis.lattice_origin, trim,
                      [&]() {
                          if (out_hit) resolve_trim_bar_lift(app);
                          return trim_bar_look(app);
                      },
                      out_hit);
}

// -- GuiPaintHandler::paint_marker_stems ---------------------------------

// THE SELECTION STEM (architect 2026-11-05, his pick a2 of tmp/s42,
// reversing 2026-11-04's coincidence stem — "dropped, along with the rest
// of cool edit stuff except a few details like the triangle playhead"; the
// full contract at the declaration): on EVERY published entry — every
// selected cue, enabled or disabled (2026-11-11), whose line reaches the
// waveform's columns (MarkerStem, render.h) — a
// SOLID ONE-W line in the `cue_outline` from the chip's apex row down the
// canvas's whole height (the head segment, below), the stereo divider's
// row crossed (architect 2026-11-02: "the divider is just another grid
// line now"), AT ITS EXACT PLACE (architect 2026-11-14, send J: the entry's
// x, drawn wherever any of the line is inside the area and cut at its
// edges, waveform_line_span), the playhead on it or not.
//
// It reads the marker painter's stash (app.marker_stems) instead of walking a
// store: the stem stands UNDER ITS CUE'S APEX, on the cue's exact place,
// and that place and its column were
// already resolved by the pass that painted the cue, on the displayed basis
// those pixels were laid out against. So the DragOverlay substitution, the
// source->target map walk, the per-marker cull and the resolver all happen
// exactly once, in the painter, and a stem can never part from
// its own chip (MarkerStem, render.h).
//
// Z-ORDER (architect 2026-11-04): THE STEM STANDS IN THE PLAYHEAD'S LINE'S
// PLACE, which precedes this pass: both are one W on one place, so while
// the playhead rests on a
// selected marker the dark stem is what shows and the ruler's head marks
// the playhead — the line under it is not drawn at all (THE COINCIDENT
// LINES, paint_playheads). The scanner follows. The full sequence is the
// paint-order block in on_redraw (build_canvas_list).
// THE ONE-STATE RULE (architect 2026-11-04, after the drag's stutter on his
// glass; the selection's half 2026-11-05, after his blink, "saw the blink
// twice in about a dozen or two selects"): THE STEM AND THE LINE ALWAYS
// AGREE WITHIN ONE PAINTED FRAME where they stand together — a selected
// marker the playhead rides, a select that lands the playhead on its
// marker, a drag or a position nudge moving both. The stem is laid out at
// the marker position THE FRAME PAINTS THE CHIP AT, on the members THE
// FRAME PAINTS SELECTED — the stash is the flag cache's, staged by the
// rebuild that drew the blitted surface and promoted with it at the frame's
// top (on_redraw), so stem and chip are one state by construction — and
// the line's position is the live cursor's. So EVERY WRITE THAT CHANGES
// WHAT THE STEM SHOWS REBUILDS THE FLAG CACHE IN THE SAME EVENT, before any
// frame can paint (Viewport::refresh_flag_cache, fingerprint-guarded, no
// plate render): every Selection mutator's publish
// (Selection::publish_selection_change, selection.cpp — the chip tap's
// select runs it before its land), the `h` view's focus and selection
// writes beside their landings (focus_history_diff_flag,
// select_history_diff_flags_modified, cycle_history_diff_flag_focus and the
// view's clears that move the playhead), the drag's per-motion ride
// (marker_drag.cpp's apply), its release (commit_drag), the position
// nudge's tail (position_nudge.cpp), and every write that kicks the
// synchronous plate rebuild, whose tail rebuilds the flags
// (kick_waveform_sync). A cursor move alone changes no stem. Nothing is
// debounced or deferred; the tick's rebuild stays as the belt.
// DAMAGE: no site of its own. The stash changes only with the flag cache's
// rebuild — the selection is a field of its fingerprint
// (FlagCache::fp_selection_hash, the `h` view's focus and selection beside
// it), and the selection's writers run the rebuild in their own event (the
// rule above) — and THAT REBUILD'S TAIL DAMAGES THE STEMS IT CHANGED
// (architect 2026-11-16, the beats; maybe_rebuild_flag_cache,
// waveform_cache.cpp): for every exact place a stem left or arrived on, the
// stem's own rect (marker_stem_damage_rect below — every cell its line
// touches, the canvas's rows with the lane's last row and the frame's line
// above them, in one rect), and the window's rows from the viewport's lanes
// through the waveform area's foot wherever the rebuild's miss is more than
// the marker-driven content (its tail's list). THE COINCIDENT LINES RIDE
// THE SAME RECT: a line that yields to a stem on its own exact place — the
// playhead's, the lead-in ring's left side — spans the stem's own columns
// on rows the stem's rect holds, so the stem's arrival or leaving repaints
// the line it hides or uncovers. The GL canvas's list is rebuilt whole at
// every frame that meets the canvas paint area.
//
// THE STEM RISES TO THE CHIP'S APEX (architect 2026-11-04, mock j1, "3
// yes"): its rect runs FROM THE MARKER LANE'S LAST ROW — the chip's apex
// row — THROUGH THE ONE-W LINE under the lane (the lane's foot line, one of
// the well's lines) TO THE CANVAS'S LAST ROW (200 %: rows 233 .. 1307,
// drawn to 1047 under the on-screen keyboard at his tuning, the canvas's
// clip cut at its top line, onscreen_keyboard::waveform_paint_area; on the
// line's real span of columns: at a whole place the column left of the
// marker's and the marker's own), so no gap stands between the apex and
// the stem (his "small gap": the rows under the lane, 234 .. 235 today,
// the canvas-only stem left). TWO PAINTERS, ONE DECISION:
// the canvas's rows are the GPU's (this pass, through the sink, whose list
// is scissored to the canvas paint area), the lane's last row and the
// frame's line the chrome's (paint_marker_stem_heads, on the backbuffer after
// the flag blit), and BOTH take THE SAME REAL SPAN OF COLUMNS from
// standing_stem_spans, off the one stash — each pixel at its exact
// coverage by that span, the sink's blended rect on the canvas and the
// coverage stamp in the chrome — so the two halves of one stem can never
// disagree. The playhead's own line stays on the canvas's rows alone.
//
// ONE SPAN PER DISTINCT EXACT x (architect 2026-11-14, send J — THE
// COINCIDENT LINES' third case, paint_playheads' paragraph the rule): two
// selected markers may stand on one frame — a drag may land a marker
// field-identical at another's position (marker_drag.cpp's commit), the
// coincident, struck pair, and an ADD SEL sweep or a select of both
// publishes two entries whose x are the same double — and two lines blended
// on one fractional place would darken its fringe (1 − (1 − a)² where one
// line gives a). So of the gated entries on one exact x ONLY THE LAST
// stands, the list's later line as the rule words it, here and so in both
// halves of the stem; the stash and every publication keep every entry.
// NOTHING LEANS ON THE STASH'S ORDER: equal x are found by sorting the
// gated places (the cues arrive ordered by column, and nothing promises
// that equal places are neighbors there — a drag's overlay and the `h`
// view's items order them by their own rules), and the spans come out in
// the stash's order, the survivors', so two stems on different places that
// overlap blend in the order they always did.
//
// THROUGH THE CANVAS SINK since 2026-10-13 (canvas_sink.h): one blended
// rect a standing stem.
std::vector<WaveformLineSpan> GuiPaintHandler::standing_stem_spans(
    const GuiRect& area) const {
    std::vector<WaveformLineSpan> spans;
    if (area.w <= 0) return spans;
    const std::vector<MarkerStem>& stems = app.marker_stems;
    // THE GATE IS THE LINE'S OWN SPAN, exactly as render_playhead's line's
    // (waveform_line_span: any of the real interval inside the area;
    // architect 2026-11-15, the no-grid arc). The producers already publish
    // only entries whose line reaches the columns (stem_meets_waveform,
    // render.cpp); the gate restates that against the area the painters
    // are handed, so no entry can leak its line into the chrome beside
    // the waveform. THE GATED ENTRIES as (exact x, stash index), sorted, so
    // each run of one x ends on its last entry — the one that stands.
    std::vector<std::pair<double, std::size_t>> places;
    places.reserve(stems.size());
    for (std::size_t i = 0; i < stems.size(); ++i)
        if (!waveform_line_span(area.x, area.w, stems[i].x).empty())
            places.emplace_back(stems[i].x, i);
    std::sort(places.begin(), places.end());
    std::vector<std::size_t> standing;
    standing.reserve(places.size());
    for (std::size_t k = 0; k < places.size(); ++k)
        if (k + 1 == places.size() || places[k + 1].first != places[k].first)
            standing.push_back(places[k].second);
    std::sort(standing.begin(), standing.end());
    spans.reserve(standing.size());
    for (const std::size_t i : standing)
        spans.push_back(waveform_line_span(area.x, area.w, stems[i].x));
    return spans;
}

// The test's gate is standing_stem_spans' own: an entry whose line reaches
// the area paints it, and only a painted stem takes another line's place.
bool GuiPaintHandler::stem_stands_at(const GuiRect& area, double x) const {
    for (const MarkerStem& stem : app.marker_stems)
        if (stem.x == x &&
            !waveform_line_span(area.x, area.w, stem.x).empty())
            return true;
    return false;
}

void GuiPaintHandler::paint_marker_stems(GuiCanvasSink& sink,
                                         const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    const GuiColor ink = palette().cue_outline;
    for (const WaveformLineSpan& span : standing_stem_spans(area))
        sink.blended_rect(span.x0, static_cast<double>(area.y), span.x1,
                          static_cast<double>(area.y + area.h), ink, 100);
}

// THE STEM'S HEAD SEGMENT, the chrome's half of the selection stem
// (paint_marker_stems' head, the rule): the marker lane's last row and the
// frame's line under the lane, on the standing stems' real spans of
// columns, in the `cue_outline` — painted after the flag blit, so it stands
// over the chip's apex, which is on its center (the triangle's tip, the
// chip's fill the stem covers there) and over the frame's line,
// and under every later
// surface (the flag editor's box, the keyboard slot, the modal box).
// THROUGH THE COVERAGE STAMP (architect 2026-11-14, send J;
// CoverageStampPass, cool_edit_paint.h): each stem one rect with real
// column edges — the very span its canvas half hands the sink — on these
// whole rows, each pixel the outline's word blended over what the lane and
// the frame's line hold there by the pixel's exact coverage; a span on whole
// columns writes the word itself on its cells.
void GuiPaintHandler::paint_marker_stem_heads(cairo_t* cr) {
    if (app.marker_stems.empty()) return;
    const GuiRect area = waveform_area(app);
    const GuiRect lane = top_marker_row_area(app);
    if (lane.h <= 0) return;
    const double y0 = static_cast<double>(lane.y + lane.h - 1);
    const double y1 = static_cast<double>(area.y);
    if (y1 <= y0) return;
    const std::vector<WaveformLineSpan> spans = standing_stem_spans(area);
    if (spans.empty()) return;
    const GuiColor ink = palette().cue_outline;
    CoverageStampPass stamp;
    stamp.begin(cr);
    for (const WaveformLineSpan& span : spans)
        stamp.rect(span.x0, y0, span.x1, y1, ink);
    stamp.end();
}

// ONE STEM'S DAMAGE RECT (architect 2026-11-16, the beats): every cell a
// stem standing at the exact place `x` (MarkerStem::x, device px from
// `area`'s left edge) touches, BOTH HALVES IN ONE RECT, so the rect holds
// the line whole — the columns its real span touches, the span's floor
// through its ceiling (waveform_line_span, the very span
// standing_stem_spans hands both painters: the sink's blended rect and the
// coverage stamp each cover exactly those cells), over the rows from the
// marker lane's last row (paint_marker_stem_heads' first) through the
// canvas's last (paint_marker_stems'). Empty (w 0) where the span is — the
// painters' own gate — or where there are no rows.
GuiRect GuiPaintHandler::marker_stem_damage_rect(const GuiRect& area,
                                                 double x) const {
    const WaveformLineSpan span = waveform_line_span(area.x, area.w, x);
    const GuiRect lane = top_marker_row_area(app);
    const int y0 = lane.h > 0 ? std::min(lane.y + lane.h - 1, area.y)
                              : area.y;
    const int y1 = area.y + std::max(area.h, 0);
    if (span.empty() || y1 <= y0) return GuiRect{area.x, area.y, 0, 0};
    const int x0 = static_cast<int>(std::floor(span.x0));
    return GuiRect{x0, y0, static_cast<int>(std::ceil(span.x1)) - x0,
                   y1 - y0};
}

// -- GuiPaintHandler::paint_strip_drag_anchor ----------------------------

// Paints the anchor stem (the Ableton pivot affordance) at the zoom anchor's
// current column, full waveform height — a live zoom gesture's, or the S
// Pen's retained one between strokes — WHITE, in the scanner's role: the
// stems that belong to the controls are white and the playhead wears its
// own two roles (architect 2026-10-09; render_strip_anchor_stem). ONE
// PRODUCER, THE TOUCH ZOOM (touch_nav_zoom.seated — the contract is at
// TouchNavZoomState, app_state.h; architect 2026-08-14, from the rig, asking
// to SEE the glass gesture: "add a zoom stem to the zoom on the touchpad just
// so I can see exactly what's going on, because at the edges there are some
// strangeness, it seems like") — the record it reads is one seat shared by
// the TWO-FINGER PINCH and, since 2026-09-25, the ONE-FINGER CTRL ZOOM
// (`TouchNavZoomState::one_finger`); AND THE SEAT'S THIRD POSTURE, THE PEN'S
// RETAINED ANCHOR (`TouchNavZoomState::retained`, architect 2026-09-27): a
// one-finger seat the pen lifted from with its side button held outlives the
// gesture, so the stem stays painted between strokes, at the anchor's live
// column, until the platform's release hook or a view-state clear takes the
// seat.
// The gate is the gesture record and nothing else, so THE SEAT ITSELF SHOWS
// THE PIVOT — the headless zoom stem — and it vanishes at the seat's clear,
// which a retained pen anchor defers past the pen's lift. The stem is the
// ZOOM PIVOT and nothing more.
// THE SEAT OWES ITS DAMAGE AT BOTH ENDS, and NEITHER END IS FREE — a seating frame is not an applied frame at all
// in the general case (the seat is taken above the gesture's exact-no-op
// return, so two fingers landing and sliding together seat and apply nothing;
// the ordering rule is at apply_touch_nav_update's seat), and even a seating
// frame that DOES apply can be dropped at the walls by
// apply_strip_drag_zoom's mid-gesture true-no-op return, which discards any
// frame whose post-clamp level and viewport both stand (a pinch that begins
// saturated is exactly that frame). So the SEAT damages at its own
// site, and the CLEAR damages through
// clear_touch_zoom_seat, because a clear can land on a frame that applies
// nothing at all (a survivor's pan refused under a modal state — the
// per-frame refusal asks state alone, the gesture's surface decided once at
// its down, architect 2026-10-16, apply_touch_nav_update).
// THE ANCHOR IS A SONG POSITION (2026-08-14, the clamped-zoom reversibility
// ruling, contract at TouchNavZoomState): the input side HOLDS the
// column (2026-10-31) and this painter PROJECTS the persisted anchor_sample,
// which sits within half a column of it, against the DISPLAYED viewport
// (wf_cache.fp_*), the same basis
// paint_playheads uses, so the stem stays locked to the
// published plate while the worker rebuilds, and the anchor lives in the active
// display domain (viewport_start + col*spp) so no warp map is walked.
// Re-projecting is what makes the stem SLIDE WITH ITS CONTENT when a clamped
// zoom keeps moving the song under it — the ruling made watchable on glass
// and the reason the architect asked for the stem there.
// strip_anchor_stem_x clamps the exact place to the visible edges' columns —
// an edge-pinned anchor draws the clamp itself.
// DAMAGE: the stem's line at its exact place touches one column more each
// side than its column's two, and every change of it is damaged whole — the
// seat and the clear each damage the waveform area (above), and every
// applied gesture frame rebuilds the plate, whose publication damages the
// canvas.
void GuiPaintHandler::paint_strip_drag_anchor(GuiCanvasSink& sink,
                                              const GuiRect& area) {
    if (!app.touch_nav_zoom.seated) return;
    if (area.w <= 0 || area.h <= 0) return;

    const PlateViewportBasis basis = plate_viewport_basis();
    if (basis.spp <= 0.0) return;

    const double anchor_sample = app.touch_nav_zoom.anchor_sample;

    // The stem's one place derivation (strip_anchor_stem_x over
    // displayed_x_at, warp_frame_map_view.h; architect 2026-11-14, send J),
    // on the PLATE basis.
    const double x = strip_anchor_stem_x(anchor_sample, basis.vp_start,
                                         basis.spp, basis.lattice_origin,
                                         area.w);
    render_strip_anchor_stem(sink, area, x);
}

// -- GuiPaintHandler::paint_sweep_highlight ------------------------------

// THE SWEEP HIGHLIGHT (architect 2026-11-12, session 114: "the add selection
// relies on the marker stems, which are dark in color, and they don't show
// exactly the bound that's being swept ... it could just be an alpha
// overlay with a config key for tuning. And it would just be a blend
// towards white, the same way that we do the playhead stem"; "highlight
// should also [go] over stem from marker/playhead (ie everything in
// waveform area)"). ONE LOOK FOR BOTH SWEEPS, TRIM's and ADD SEL's, SHOWN
// ONLY WHILE THE SWEEP'S POINTER IS DOWN: one tint record (GuiCanvasSink::
// blended_rect) over the columns sweep_highlight_columns answers
// (app_state.h — the real span, the formers and the empty states; its
// damage sites read the same owner's cells, sweep_highlight_rect and
// sweep_highlight_change),
// blending it toward WHITE at the `sweep_highlight` device key's
// whole percent (a tuning key, device_config.h; 0 paints nothing), read
// here at every list build.
// WHITE IS SPELLED HERE, no palette role and no theme tone: "a blend
// towards white", the scanner's canonical white the precedent (render.h's
// scanner paragraph).
// THE LIST'S LAST RECORD (build_canvas_list): it blends over everything the
// canvas list draws in the waveform area — the ground, the grid, the
// plate's ink and magnify core, the center lines, the stereo divider, the
// lead-in ring, the playhead's line, the selection stems, the scanner and
// the zoom anchor's stem — so every line under it is lightened with the
// canvas (the one exception to the lines' own color, canvas_sink.h's head),
// and nothing in the list paints over it. A record that changes under it
// owes the damage it always did (canvas_sink.h's damage contract); every
// frame draws the whole list, this tint included (present_frame's THE
// WHOLE DRAW).
void GuiPaintHandler::paint_sweep_highlight(GuiCanvasSink& sink,
                                            const GuiRect& area) {
    if (area.w <= 0 || area.h <= 0) return;
    // THE SPAN'S REAL EDGES (architect 2026-11-14, send J;
    // sweep_highlight_columns): TRIM's the trim bar's own span edge for
    // edge, ADD SEL's from the down's real place to the pen's (architect
    // 2026-11-15, the no-grid arc) — one blended rect over the canvas's
    // rows, each cell an edge crosses lightened by its coverage's share of
    // the percent.
    const SweepHighlightColumns span = sweep_highlight_columns(app, audio);
    sink.blended_rect(span.x0, static_cast<double>(area.y), span.x1,
                      static_cast<double>(area.y + area.h), hex(0xFFFFFF),
                      app.device_config->sweep_highlight);
}

// -- GuiPaintHandler::paint_playheads ------------------------------------

void GuiPaintHandler::paint_playheads(GuiCanvasSink& sink,
                                      const GuiRect& area) {
    // THE LINE'S COLOR IS THE PALETTE'S `playhead_line`, ONE STATIC TONE
    // (architect 2026-11-04 night; the rule at render_playhead's paragraph):
    // no clock, no stored color, nothing damaged at rest. The head in the
    // ruler wears its own `playhead_head`.
    // Use the displayed viewport AND its samples-per-pixel
    // (wf_cache.fp_vp_start, derived spp) so the cursor stays in
    // lockstep with the cached waveform / stem / flag layers during
    // the 1-2 paint frames while the worker rebuilds against a
    // viewport change. See declaration comment in app_state.h.
    const PlateViewportBasis basis = plate_viewport_basis();
    const int px_x = playhead_pixel_x(app, wf_cache.fp_vp_start, basis.spp,
                                      basis.lattice_origin);
    // THE LINE'S EXACT PLACE (architect 2026-11-14, send J), the column's
    // own value on the same basis: where the line is painted and what its
    // span is gated on, the column above its cull's and its damage's.
    const double x = playhead_exact_x(app, wf_cache.fp_vp_start, basis.spp,
                                      basis.lattice_origin);
    // THIS PASS IS THE CURSOR'S CANVAS LINE, nothing else: its HEAD is the
    // triangle in the ruler (paint_ruler_row); across the view bar
    // (architect 2026-10-09 ~14:30) and the marker lane the playhead draws
    // nothing.
    //
    // THE CURSOR PLAYHEAD ALWAYS PAINTS (architect 2026-07-30): ONE playhead
    // form at the resting cursor column whatever the selection is doing — a
    // SOLID ONE-W LINE in the palette's `playhead_line` (Default
    // 81E1EA, architect 2026-11-12; render.h's playhead paragraph), opaque
    // on every pixel it covers whole. It paints
    // BEFORE paint_marker_stems, whose selection stem stands over it
    // (architect 2026-11-04; the stem the selection's since 2026-11-05),
    // and before the scanner.
    //
    // THE COINCIDENT LINES (architect 2026-11-14, send J, "Agree to all
    // decisions." — the one rule about lines that overlap): A LINE IS NOT
    // DRAWN WHERE A LATER LINE OF
    // THE LIST, OF THE SAME WIDTH, STANDS ON THE SAME EXACT x (the doubles
    // equal) over rows that hold its own. THREE CASES EXIST: this line under
    // a selection stem — the common state, every select landing the
    // playhead on its marker, the marker and the playhead parked on it one
    // whole displayed frame and so one double through displayed_x_at — the
    // lead-in ring's left side under its reset's stem
    // (paint_phase_reset_overlay_ring), and a selection stem under another
    // on the same frame, two selected markers coincident
    // (standing_stem_spans, which answers one span a distinct x). WHY: each line is blended over what
    // lies beneath by its coverage, so two lines on one fractional place
    // would leave the lower one's color in the upper's fringe (at half
    // coverage a quarter of it) — a light halo round the dark stem; at a
    // whole place the lower line was hidden whole, so nothing changes
    // there. Lines at different places blend in list order as they fall
    // (accepted). The test is stem_stands_at, on the stems the stem pass
    // will paint. THE TWO x ARE ONE EXPRESSION ON ONE BASIS AT REST: the
    // stem's is the flag pass's, laid out on the plate's published span
    // over its width (wf_cache.fp_* as the flag cache's rebuild read them),
    // this line's the plate basis's (the same fields now), so the doubles
    // are equal whenever the flags stand on the standing plate. Between a
    // plate's publication and the flag cache's next rebuild (an async
    // publish window, a resize) the two part, the test answers no and the
    // line is drawn under its stem as any two lines fall.
    if (stem_stands_at(area, x)) return;

    // THE SCANNER LEFT THIS PASS (architect 2026-08-01) — it is paint_scanner,
    // invoked after this one, solid over everything in the canvas while it
    // runs, this cursor included.
    render_playhead(sink, area, px_x, x, palette().playhead_line);
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
// SO THE SCANNER STANDS OVER EVERY LINE IN THE WAVEFORM AREA while it runs —
// over the stems, over the cursor where they overlap, over the plate; only
// the zoom anchor's stem and a standing sweep highlight (which lightens it
// with everything else, paint_sweep_highlight, architect 2026-11-12) come
// after it. Everything it covers is a per-frame repaint anyway.
//
// THE SCANNER IS THE MOVING LINE, and it HAS ITS OWN ROLE, `scanner`
// (architect 2026-10-05), WHITE (architect
// 2026-10-07: "let's go back to a white scanner") — SOLID, ONE W wide
// (architect 2026-11-04, the unit, waveform_line_px's rule, render.h), AT
// THE PREDICTOR POSITION'S EXACT PLACE (architect 2026-11-14, send J: the
// line moves by the real distance the position moved, so at a coarse zoom
// it glides a fraction of a px a frame where its rounded column ticked one
// px every several frames — the jitter he saw there, "I can see clearly";
// while it plays, every painted frame then differs and is presented, and
// no gate is added to avoid it), the canvas's rows alone (Cool Edit's
// playback cursor is a solid line, measured 2026-10-09), and NEVER BLINKING
// (the resting cursor's line is a static tone of its own since 2026-11-04
// night).
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
    const int scan_px =
        scanner_pixel_x(app, wf_cache.fp_vp_start, basis.spp,
                        basis.lattice_origin);
    // The predictor position's exact place (architect 2026-11-14, send J):
    // the line moves by the real distance the position moved, the column
    // above its cull's alone; its damage is this line's own cells, the hull
    // of its span on these rows (scanner_invalidate_rect, main.cpp).
    const double scan_x =
        scanner_exact_x(app, wf_cache.fp_vp_start, basis.spp,
                        basis.lattice_origin);
    render_playhead(sink, area, scan_px, scan_x, palette().scanner);
}

// -- THE BOTTOM ROW'S MODAL STATE ----------------------------------------
//
// THE BOTTOM ICON ROW IS THE PAGE'S THAT STANDS IN THE VIEW (modal_box.h, the
// one owner of the family's shape; architect 2026-11-19, send BB; the two
// pages 2026-11-20, send EE; the rule at modal_owns_bottom_row below): an
// overlay's controls row stands on the row's rows under the family's box
// while an overlay stands, A QUESTION'S ANSWERS AND THE PROGRESS PAGE'S
// CANCEL stand there while one of those pages does (THE QUESTION PAGE AND
// THE PROGRESS PAGE, at paint_modal_dialog's plan), and under a line
// editor's sub-page the row is the on-screen keyboard's (THE VALUE SUB-PAGE
// below). NOTHING ABOUT THE
// RENDERER MOVED for it — one window, one surface, one frame, one painter —
// so this is emphatically not the scrapped second-toplevel model
// (closed_questions.md carries that do-not-re-propose). WHILE AN OVERLAY OR
// A PAGE STANDS, OR THE ON-SCREEN KEYBOARD STANDS OVER THE ROW (every
// editor; send EE, onscreen_keyboard.h's placement block), THE ROW YIELDS
// WHOLE: all TWENTY-ONE buttons — the COPY /
// PASTE duo's two, the skips' two, the VERB GROUP'S EIGHT, the
// step-and-walk group's four and the nudge-and-play group's five — and the
// left run (the timestamp, the pipe and the tab's name) stand down, nothing
// negotiates for space, and the overlay's controls or the page's answers
// take the content band they left (paint_modal_dialog; the box is
// paint_modal_box's, laid by paint_folder_overlay under the render player
// and the picker and by paint_modal_dialog for the three pages and the
// score viewer's page).
//
// The helper below is that fork, shared by the row's painter and the
// clock's fingerprint (which want the boolean) — one answer, so the row
// cannot yield to an overlay's row the modal painter would then decline to
// paint.

// "THE ROW BELONGS TO THE MODAL FAMILY" — THE BOTTOM ICON ROW IS THE PAGE'S
// THAT STANDS IN THE VIEW (architect 2026-11-19, send BB: "actually windows
// like score and player need the bottom row, and so thy might as well keep
// the status bar, which sets the precedent that overlays keep them"; his "i
// take your recommends for all": one rule for both icon rows; the two pages
// architect 2026-11-20, send EE). The row is the family's EXACTLY WHEN:
//   * A QUESTION'S PAGE OR THE PROGRESS PAGE STANDS (modal_page_stands,
//     app_state.h), over the main page, the `h` view or an overlay alike:
//     the row is the page's ANSWERS — Arc's word buttons flush right, the
//     question's in its table's order, the progress page's one Cancel — and
//     nothing else, no timestamp, no tab name, no roster key and none of an
//     overlay's own controls (paint_modal_dialog's plan);
//   * AN OVERLAY STANDS — the RENDER PLAYER (its transport row), the PICKER
//     in its five contents (the project picker's and the value list's
//     Cancel, the settings page's Close, the trim presets list's cluster,
//     the BPM form's RENDER and Cancel) or THE SCORE VIEWER (its keys, scrub
//     and page cell or clock): that overlay's controls row, live.
// Then paint_modal_dialog paints that row on the row's rows — and the top
// icon row lights the same page's opener (roster_face_page_owner,
// app_state.h), greyed whole under a page, the opener lit and inert
// (redesign_button_enabled's first arm, send EE). AND UNDER EVERY EDITOR THE
// ROW IS NOBODY'S: the on-screen keyboard stands over it (architect
// 2026-11-20, send EE: "the bottom row remains inactive. So simply showing
// it isn't helpful"; "on that page, the only controls are the osk's esc and
// enter"; onscreen_keyboard.h's placement block), so the row yields under
// the flag editor and under a line editor's sub-page alike
// (paint_bottom_strip: the lane's ground laid and nothing on it, no key,
// timestamp or word — the keyboard covering it, a strip of that ground
// showing where a keyboard tuned short leaves the lane's top rows clear) —
// this fork answers who owns the row where it shows, the keyboard's
// standing beside it at each reader.
static bool modal_owns_bottom_row(AppState& app) {
    if (modal_page_stands(app)) return true;
    if (app.render_player.active) return true;
    if (app.picker.active) return true;
    // THE SCORE VIEWER (the sixth, 2026-10-26): its PREV / NEXT, scrub, page
    // cell and range keys take the lane whole, as the player's row does.
    return app.score_viewer.active;
}

// (THE BORDER UNDER ROW 8 — paint_bottom_border, the dock bar's six rows on
// a bottom lane of their own under row 8, 2026-10-14, row 8's bars running
// through its light row from 2026-10-21 — IS REMOVED, architect 2026-10-22,
// the simple layout: row 8 stands on the window's foot.)

// -- GuiPaintHandler::paint_bottom_strip ---------------------------------

void GuiPaintHandler::paint_bottom_strip(cairo_t* cr) {
    // THE UNIFIED BOTTOM ROW (architect-ruled 2026-08-12, rows 8 and 9 merged
    // into ONE lane, on the WINDOW'S FOOT; THE STRIP since the simple layout,
    // 2026-10-22; bottom lane 1, over the status bar): one plain-text run
    // at the left — the timestamp, a text pipe and the active tab's name
    // (bottom_left_run) — and, packed against the right edge, the COPY /
    // PASTE duo, the skips, the MARKER-VERB GROUP, the step and walk and the
    // nudge and play with Play last, each one linked group (the tables
    // above own those memberships). That is the whole roster. This painter
    // owns the lane's CHROME (its ground) and nothing else; the
    // buttons and the clock are paint_bottom_row_buttons_and_clock, called
    // from here onto the grounded band (the cluster's tables and the clock's
    // metrics live beside that body).
    // THE ROW HAS A MODAL STATE since 2026-08-13, in which those tenants
    // stand down and the modal family's controls row takes the row — while
    // an overlay, a question's page or the progress page stands (2026-10-14;
    // send BB 2026-11-19; send EE 2026-11-20; the ruling and the fork are
    // at modal_owns_bottom_row, just above; the modal's own layout is
    // paint_modal_dialog's) — and with the chain gone
    // there is nothing left on the lane to negotiate with, which is what
    // makes that yield clean. UNDER EVERY EDITOR THE ROW YIELDS TO THE
    // ON-SCREEN KEYBOARD standing over it (below).
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
    // platform.h's head — the timestamp stands empty and no tab is named
    // (clock_time_text, the name's block), and the buttons must be visible
    // on every frame class their press claim is live on.
    const GuiRect lane = bottom_row_area(app);
    if (lane.w <= 0 || lane.h <= 0) return;

    // THE LANE'S GROUND, ON EVERY FRAME WHATEVER STANDS OVER IT: THE BODY
    // TONE (the palette's `face`, the theme's `body_face`; send Z,
    // 2026-11-19), the lane the window's width over the status bar — laid
    // under the on-screen keyboard too, which covers it in the same pass
    // (paint_keyboard_slot paints after this, opaque). The row's tenants are
    // the
    // program's (row 8, and the modal family's controls row — every
    // overlay's, modal_box.h, 2026-10-14), so this lane never changes for
    // them. The ground erases whatever render_background laid down, so the
    // strip does not depend on that erase happening to hold the same value.
    // The overlays paint their controls on this ground and lay none of
    // their own (paint_modal_dialog).
    {
        cairo_save(cr);
        cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
        paint_cell_rect(cr, lane, palette().face);
        cairo_restore(cr);
    }

    // THE ROW YIELDS TO THE MODAL FAMILY'S ROW (2026-08-13; the fork and the
    // ruling are at modal_owns_bottom_row just above). The ground above is
    // the ROW'S whatever it carries — an overlay's CONTROLS ROW and a
    // page's ANSWERS paint on the row's own ground, laying none of their own
    // (the box stands above the row, over the canvas column, modal_box.h) —
    // and every other tenant stands down here: paint_modal_dialog owns the
    // row from this frame until the overlay's closer or the page's leave.
    //
    // THE ROW'S BUTTONS PUBLISH ZERO RECTS rather than stranding the last
    // frame's (the roster's own model — a zero/invalid stash contains no
    // point), so nothing can hit an unpainted button and no consumer of those
    // rects can read a phantom bound. Their THREE FACE BITS ARE
    // still published: those are read by main.cpp's staleness comparator
    // alone, and a stash frozen for the overlay's whole life would drift the
    // moment the open changed one — an overlay's open STOPS PLAYBACK, which
    // flips the transport button's GLYPH (the same edge flipped that
    // button's ENABLED bit while the row was honest, and the play/stop
    // pair's LAMP while it was a radio; the state has moved axis twice and
    // the example holds on each) — leaving the comparator to
    // invalidate this row on every tick with nothing to repaint but the
    // overlay's row.
    //
    // THE CLOCK'S CELL ZEROES WITH THEM, which is what makes the row's one
    // rect owner degrade HONESTLY: a zero cell answers the WHOLE LANE at
    // clock_invalidate_rect (main.cpp), which covers the overlay's controls
    // row standing on it (the modal's whole surface from 2026-08-13 until its
    // box of 2026-10-14; the box takes the modal family's area,
    // invalidate_modal_dialog_area).
    // NOTHING READS THESE ZEROES as a bound — since 2026-08-13 nothing on
    // this row is measured from a button stash at all, the status chain having
    // taken its right anchor to the tab row with it.
    // AND THE ROW YIELDS WHILE THE ON-SCREEN KEYBOARD STANDS (2026-10-10, ON
    // SCREEN IS AS PAINTED; architect 2026-11-20, send EE: "the bottom row
    // remains inactive. So simply showing it isn't helpful"): the keyboard
    // stands over this row and the status bar (onscreen_keyboard.h's
    // placement block) — an editor's answers are the keyboard's Enter and
    // Esc, the flag editor's case too, the one editor that raises the
    // keyboard with no dialog — so the row holds nothing: the ground above
    // is laid and covered by the keyboard in the same pass, and a key nothing
    // shows must not hover, wear a tooltip or answer a press (the zero
    // rects), nor its clock tick damage anything (the zero cell; the clock's
    // owner answers an empty rect under the keyboard, clock_invalidate_rect).
    if (modal_owns_bottom_row(app) || onscreen_keyboard::stands(app)) {
        // The right block's groups stand down together: the SKIPS, the
        // MARKER VERBS, the STEP-AND-WALK GROUP, the NUDGE-AND-PLAY GROUP
        // with Play, and the `h` view's Older / Newer in the nudge pair's
        // seats. Each is a tenant of this lane like every other member, and
        // the overlay's row or the keyboard takes the lane whole: the block's one table of groups
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
        // The cell's pixels are the yielded row's now, so this target's
        // record of the clock no longer holds (GuiClockRecord).
        app.clock_painted[static_cast<std::size_t>(gui.painting_target())]
            .valid = false;
        return;
    }

    // THE BUTTON CLUSTER AND THE CLOCK — publishes the clock's reserved cell
    // (AppState::clock_cell_rect), which the row's one remaining rect owner
    // reads (clock_invalidate_rect, main.cpp). Runs whole on every exposure
    // like the top button rows (it shapes no text beyond the clock's one
    // memoised-cell run, so a narrow clip pays almost nothing for it) and is
    // the LAST thing this painter does: the row's other tenant is the active
    // tab's name beside the clock, painted by the same function; the process
    // line stands in the status bar under the row (paint_status_bar).
    paint_bottom_row_buttons_and_clock(cr);
}

// -- GuiPaintHandler::paint_modal_dialog ---------------------------------
//
// THE MODAL SURFACE. It hosts THE OVERLAYS' CONTROLS ROWS — the render
// player's, the picker's contents' (the project picker, the settings page,
// the value list, the trim presets, the BPM form) and the score viewer's —
// and THE THREE PAGES: A QUESTION'S (every prompt), THE PROGRESS PAGE and
// THE VALUE SUB-PAGE (the settings editor's buffer in its three kinds:
// settings value, trim preset name, BPM form field). The top-strip FLAG
// editor is deliberately NOT one — it is positional, editing the marker
// where it stands (render_flag_editor_box), a modal by its veil alone
// (keyboard_modal_editor_active).
//
// ONE RENDERER, ONE WINDOW, ONE FRAME — unchanged. Nothing here creates a
// surface, a window or a second buffer: every page is pixels this body
// paints into the one frame, so
// this is NOT the scrapped second-toplevel model (that attempt was reverted
// byte-exact and is recorded do-not-re-propose in closed_questions.md). The
// bottom row yields whole while an overlay or a page stands —
// modal_owns_bottom_row, above, is the shared fork.
//
// THE OVERLAYS STAND IN THE MODAL FAMILY'S BOX AND ON ITS CONTROLS ROW
// (architect 2026-10-14, modal_box.h, the one owner of the shape): their
// controls on the bottom icon row's rows, the outermost boxes 6 W inside
// the panel (modal_controls_seats). THE TWO PAGES STAND THE SAME WAY, the
// view their page and the row their answers (THE QUESTION PAGE AND THE
// PROGRESS PAGE, the owner block at this body's plan; architect 2026-11-20,
// send EE):
//   A PROMPT — its question one line of plain Regular 12 W on the view, its
//   answers Arc's word buttons on the controls row, each its PLAIN WORD
//   ("Save", "Discard", "Cancel", "Retry", "Yes", "OK", "Delete"), the
//   leave word last (kModalCancelLabel's rule), no letter underlined
//   (architect 2026-10-30: the buttons are the prompt's one road), and NO
//   BUTTON FOCUSED (architect 2026-11-02: a prompt has no focus, the rule
//   at PromptState).
//   THE PROGRESS PAGE (AppState::BatchProgress) — its sentence, the bar
//   over the entries done, the count, and Cancel alone on the controls row,
//   unfocused as a prompt's buttons are.
// A LINE EDITOR IS A SUB-PAGE OF ITS OWN (architect 2026-11-20, send EE; THE
// VALUE SUB-PAGE above, paint_value_page): the view one flat page with the
// value's label and Arc's focused entry alone on it, ONE LINE that SCROLLS
// SIDEWAYS, GTK's way (architect 2026-11-19, send AA: "The pen does have a
// good way to scroll, and those are the left and right arrows of the
// keyboard. That's exactly why they're there."), NO BUTTON — the on-screen
// keyboard, over the view's foot and the bottom icon row (the box and the
// page cut at its top line), is the page's one control, its Enter the
// commit and its Esc the cancel. The line is the existing text_editor
// machinery — selection, caret, click-to-caret, byte-identical editing —
// through the scroll offset (text_editor::State::view_offset_px), and a
// refused Enter selects the whole text, nothing recolored, its owner's
// card saying why (architect 2026-10-03; render.h's palette block, A
// REFUSED ENTER RECOLOURS NOTHING).
//
// EVERY BUTTON CARRIES A TOOLTIP (architect 2026-08-13: "we just do a tooltip
// just like the regular icon tooltips"), through the roster's own machinery
// end to end — the same Qt timing, the same box, the same painter, the same
// AppState::redesign_tooltip state, whose owner names either surface now. The
// TEXT is a word button's word alone, a glyph button's the player's own,
// published in the stash beside the rect; the WAIT is written by this surface's own hover walk
// (update_modal_dialog_hover), and as everywhere no wait starts under a held
// press (the model is at AppState::RedesignTooltip).
//
// THE BUTTONS ARE ARC'S (architect 2026-11-18 / 2026-11-19, send Z's stage
// C; materials.h's THE KEY): a controls row's GLYPH KEYS Arc's linked buttons — a cluster one linked
// group, a lone key a group of one (paint_linked_group), its drawing on
// each (paint_key_glyph) — and its WORD BUTTONS Arc's text buttons, the
// word alone, no glyph (his ruling 2026-11-18; paint_word_button), laid out
// by modal_word_buttons, player_row_layout and the other rows' layouts; each
// in Arc's states — at rest, LIT in the accent for the row's one checked
// button (Repeat One) and under a standing press, disabled where its owner
// publishes the bit — a question's answers and the progress page's Cancel
// word buttons of the same row walk. The word buttons' tooltip is the word.
// NO BUTTON HAS A FOCUS (no key reaches a surface with buttons: the
// on-screen keyboard stands only under an editor, and an editor has none).
// AT REST UNDER THE HOVERING PEN EACH WEARS ARC'S
// HOVER FACE (send CC, 2026-11-20), AND EVERY CHANGE OF ITS STATE FADES AS
// ARC'S SHEET SAYS (send DD, 2026-11-20; the rule AppState::FaceFades's,
// each button painted as it stands on screen). (Windows' push buttons, 2026-10-02 to
// 2026-10-22, are git history.)
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
// METRICS. The seats are the controls row's (modal_controls_seats: 6 W
// inside the panel, no tail): every glyph key the program's key at the
// case's seat, a group's keys sharing one seam column each (materials.h's
// key_link_pitch_px; the player's player_row_layout and the score viewer's),
// the word buttons one width, 4 W apart (modal_word_buttons). (The 6-W
// inter-button gap, the old popup's 8 laptop px, stood until 2026-10-23.)
// The three pages' lines are THE PAGE MARGIN's (modal_box.h's
// modal_page_lines).
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
// modal's surface — the modal family's whole area (modal_area_rect: the box
// and the controls row) under the score viewer and the three pages; the
// controls row under the render player and the picker — DAMAGE'S FALLBACK,
// read where no narrower rect is known (a press face with no stash index).
// Damage: the openers and a question's raise, replacement and leave
// invalidate the whole window (no surface exists before the first paint;
// the page takes the view from whatever stood there and gives it back); a
// press edge on a button its face (invalidate_modal_press_face, architect
// 2026-11-16, the beats); a standing line editor's edits and caret blink
// the entry's box (Viewport::invalidate_dialog_editor_box: the entry is a
// fact of the view, never moving or growing with the text); a progress step
// the bar's rows through the count's line (set_batch_progress_index;
// modal_box.h's ModalPageLines::step); every other closer rides
// invalidate_modal_dialog_area, which takes the bottom icon row's lane and,
// while a page stands or was last painted, the modal family's whole area —
// the rect a closer owes (viewport.cpp).

namespace {

// (THE WORD BUTTONS' WINDOWS METRICS — the 6-W button gap, Windows' 75 x
// 23-W push button sized to its label between the spec's pads, the editor's
// one-row field's 23-W height and 5-W pad — stood 2026-10-02 to 2026-10-23;
// every word button is the program's key since, modal_word_buttons
// below. Git history holds their derivations.)
// (THE FOCUS FRAME — kModalFocusFramePx, one relief line outside the
// keyboard-focused button, Windows' default-button frame, 2026-10-02 to
// 2026-10-23 — is not painted, architect 2026-10-23: the button walk's NO
// FOCUS FRAME.)
// THE PLAYER ROW'S SPACING IS THE RUN GAP, A GTK BOX AT 6 W (architect
// 2026-11-03, on mock dec7b; the box's spacing 2026-11-18 / 19, send Z's
// design, built in send AA): between every two neighbors (the left
// cluster → the scale's trough, the trough → the position, the position →
// the length, the length → the transport) stands render.h's
// program_run_gap_px, the band's run gap (the `run_gap` tuning key's W
// since 2026-11-07, 6 its default since send Z); the score viewer's two
// rows, laid
// in the player's shape, read it alike (score_row_layout,
// score_media_row_layout). A wider gap is taken from the scrub's trough,
// which the rows' floor of two knobs drops whole when too little is left.
// The row's own 8-W space is retired
// (closed_questions.md).

// (THE WORD ROWS NO PROMPT TABLE HOLDS are the picker's and the progress
// page's one button. A BUTTON'S WORD SAYS WHAT LEAVING DOES: the rule, the
// two leave words kModalCancelLabel and kModalCloseLabel and which overlays
// carry a leave button (every one without a scrub, architect 2026-11-11,
// session 107) are app_state.h's (the lit opener's tooltip under its
// overlay reads the words there too, overlay_leave_word). A line editor has
// no button: its answers are the on-screen keyboard's Enter and Esc.)

// THE WORD BUTTONS' LAYOUT — ONE FOR THE PAINTER AND THE PRESS: ARC'S TEXT
// BUTTONS (architect 2026-11-18, his ruling: the word alone, no glyph; send
// Z's stage C, 2026-11-19; the kit's word_width / word_row, GtkButtonBox's
// homogeneous width, measured on his 9 pt desktop: Help 1340..1424, Close
// 1747..1831) — on `seats` (the controls row's, modal_controls_seats) the
// words `words`, in painted order, each a box on the case's rows (the key's
// height, 28 W at the default) of ONE WIDTH FOR THE ROW, max(kWordButtonMinW
// (85 W), the widest word's advance in the Body face (Regular 12 W) rounded
// up to a whole px + kWordButtonPadsW (34 W)) — 170 device px on the tablet
// for every word the rows hold today — kWordButtonGapW (4 W) apart, THE
// CLUSTER FLUSH RIGHT, its last box's right edge the row's right seat
// (6 W inside the panel); wider than the row it starts at
// the row's first column (THE BUTTONS STAY WHOLE). The word is the button's
// tooltip and its one print (word_button_ink).
constexpr double kWordButtonMinW  = 85.0;
constexpr double kWordButtonPadsW = 34.0;
constexpr double kWordButtonGapW  = 4.0;
struct ModalWordButtons {
    std::vector<GuiRect> rect;
};
// THE HOMOGENEOUS WIDTH of a row of word buttons (the kit's word_width):
// max(85 W, the widest word's advance in the Body face rounded up + 34 W).
int word_button_w_px(std::span<const std::string_view> words) {
    const GuiFont font = gui_font(GuiFace::Body);
    double widest = 0.0;
    for (const std::string_view w : words)
        widest = std::max(widest, text_shape::memoized_run(font, w)->width_px);
    return std::max(scaled_px(kWordButtonMinW),
                    static_cast<int>(std::ceil(widest)) +
                        scaled_px(kWordButtonPadsW));
}
ModalWordButtons modal_word_buttons(const BottomRowSeats& seats,
                                    std::span<const std::string_view> words) {
    ModalWordButtons out;
    if (words.empty()) return out;
    const int w = word_button_w_px(words);
    const int gap = scaled_px(kWordButtonGapW);
    const int n = static_cast<int>(words.size());
    const int total = n * w + (n - 1) * gap;
    int x = std::max(seats.cases_left, seats.cases_right - total);
    for (int i = 0; i < n; ++i) {
        out.rect.push_back(GuiRect{x, seats.case_y, w, seats.case_h});
        x += w + gap;
    }
    return out;
}

// THE PROGRESS BAR (GTK's, the design's §G; the progress page's, its seat
// modal_box.h's ModalPageLines::bar): the trough `scale_trough` (our
// own ScaleTrough, arc_dark 2D303B) on [x0, x1) x [y, y + h), fully rounded
// (radius h / 2: 3 W), the done part `cue`, the accent, from x0 over `frac`
// of the length (at least the trough's height, so its two round ends stand
// whole), rounded the same; at exact coverage on the stamp.
void paint_progress_bar(cairo_t* cr, double x0, double x1, double y, double h,
                        double frac) {
    if (!(x1 > x0) || !(h > 0.0)) return;
    const double rad = h * 0.5;
    CoverageStampPass pass;
    pass.begin(cr);
    pass.rounded_rect(x0, y, x1, y + h, rad, rad, palette().scale_trough);
    if (frac > 0.0) {
        const double end =
            x0 + std::max(std::min(frac, 1.0) * (x1 - x0), h);
        pass.rounded_rect(x0, y, std::min(end, x1), y + h, rad, rad,
                          palette().cue);
    }
    pass.end();
}

// THE TWO PAGES' LINES, PAINTED (THE QUESTION PAGE AND THE PROGRESS PAGE, at
// paint_modal_dialog's plan; every seat modal_box.h's modal_page_lines) on
// the view the caller has just laid (paint_modal_box, the lists' tone):
//   A QUESTION (`question`): AppState::prompt's text, ONE SHAPED RUN in the
//     Body face (Regular 12 W) in the lists' rows' tone
//     (surface_text(GuiSurface::ListRow)), at the page's pen and baseline,
//     CUT at the page margin inside the view's right edge on its own mask
//     (text_shape::show_shaped_run_within, the lists' names' road) — it
//     never wraps;
//   THE PROGRESS PAGE: AppState::BatchProgress's sentence the same way; THE
//     BAR over the entries done (paint_progress_bar, done_fraction); THE
//     COUNT in GuiFace::Caption (Regular 10 W) in the same tone at the same
//     pen and cut.
// Each text and the bar under its own exposure (a progress step's damage is
// the bar's rows through the count's line, so the sentence is not reshaped
// for it). Publishes nothing: the answers are the controls row's
// (paint_modal_dialog's plan walk).
void paint_modal_page(cairo_t* cr, const AppState& app, bool question) {
    const ModalPageLines l = modal_page_lines(app);
    if (l.view.w <= 0 || l.view.h <= 0) return;
    const double pen = static_cast<double>(l.pen_x);
    const text_raster::XSpan span{pen, static_cast<double>(l.text_end_x)};
    if (!(span.x1 > span.x0)) return;
    cairo_save(cr);
    cairo_rectangle(cr, l.view.x, l.view.y, l.view.w, l.view.h);
    cairo_clip(cr);
    const AppState::BatchProgress& bp = app.batch_progress;
    if (clip_meets(cr, l.sentence_line)) {
        const text_shape::ShapedRun run = text_shape::shape_text_run(
            gui_font(GuiFace::Body), question ? app.prompt.text : bp.sentence());
        set_palette_source(cr, surface_text(GuiSurface::ListRow));
        text_shape::show_shaped_run_within(cr, run, pen, l.sentence_baseline,
                                           &span, 1);
    }
    if (!question) {
        if (clip_meets(cr, l.bar))
            paint_progress_bar(cr, static_cast<double>(l.bar.x),
                               static_cast<double>(l.bar.x + l.bar.w),
                               static_cast<double>(l.bar.y),
                               static_cast<double>(l.bar.h),
                               bp.done_fraction());
        if (clip_meets(cr, l.count_line)) {
            const text_shape::ShapedRun run = text_shape::shape_text_run(
                gui_font(GuiFace::Caption), bp.count());
            set_palette_source(cr, surface_text(GuiSurface::ListRow));
            text_shape::show_shaped_run_within(cr, run, pen, l.count_baseline,
                                               &span, 1);
        }
    }
    cairo_restore(cr);
}

// -- THE VALUE SUB-PAGE (architect 2026-11-20, send EE) ----------------------
//
// A VALUE'S EDITOR IS A SUB-PAGE — THE RULE'S ONE OWNER (his words: "the
// settings page and presets page would then have a subpage where if the user
// clicks on a value a single line text input is shown, like in a setting
// editor where oyu can type in eg the path to a folder. it shuodl be
// arc/gtk's text input fiel, with arc's metrics. on that page, the only
// controls are the osk's esc and enter. same with all pages that require
// osk - the only controls are the osk's. so we may need tocreate separete
// subpages to accomodate that. the osk should essentially stop all other
// activity and become the only active element when it's onscreen."; "we
// shuold remove the popups - it seems those come from gnome and xfce just
// inherited them"; to "the sub-page's content: the setting's name as a label
// above the entry, nothing else": "subpage yes"; on the mocks: "settings
// prefer toplaigned"; to "the pen still seats the caret in an editor's
// text": "keep - i meant only live element aside from the text edit box";
// on the page margin: "1 agree").
// WHILE ONE OF THE THREE LINE EDITORS STANDS (the settings editor's buffer:
// a settings value, the trim preset's name, a BPM field —
// AppState::dialog_editor_session) THE VIEW (the box's interior,
// modal_box_interior_rect: on the tablet rows 142..1307, the panel's whole
// width) IS ONE FLAT PAGE in the lists' tone (`ruler_face`, the box
// paint_modal_dialog lays) AND ON IT, TOP-ALIGNED, NOTHING BUT (every seat
// modal_box.h's modal_page_lines, THE PAGE MARGIN):
//   THE LABEL — the value's name and a colon (AppState::value_page's `name`:
//     the settings row's key, "projects_path:"; the BPM field's key; under
//     the trim preset's name editor the act's own name, "Save As:"), plain
//     Regular 12 W in the lists' rows' tone
//     (surface_text(GuiSurface::ListRow)), its pen 12 W from the view's
//     left, ITS CAP BAND'S TOP 13 W UNDER THE VIEW'S TOP (a label with no
//     capital seats by the face's cap band all the same), cut 12 W inside
//     the view's right edge on its own mask, never wrapped;
//   ONE SINGLE-LINE ARC ENTRY, FOCUSED (materials.h's paint_entry: the
//     canvas's tone in a one-W `well_border` ring, the inner one-W line of
//     the accent — Arc's entry:focus, the entry holding the focus for as
//     long as the page stands), from 12 W inside the view's left edge to
//     12 W inside its right, kEntryHeightW (28 W) tall, ITS TOP 6 W UNDER
//     THE LABEL'S LINE BOX; its text the buffer — the value alone
//     (settings_editor.h's head), the BPM field's value, the preset's name —
//     Regular 12 W at pen kEntryPenW (9 W) inside its left edge on the
//     line-box baseline (+18 W of its 28), the caret one W in the field's
//     text tone over the line box's rows, ALWAYS SHOWN (its blink the
//     tick's), the selection the selection pair.
// ON THE TABLET (200 %): the label's pen x 24, its line box rows 163..190,
// its baseline 185, the painted caps' first row 168; the entry x 24..2279,
// rows 203..258 (1128 x 28 W), its text's pen x 42, its inner width
// 42..2261.
// THE ENTRY IS ONE LINE AND SCROLLS SIDEWAYS, GTK'S WAY (architect
// 2026-11-19, send AA: "The pen does have a good way to scroll, and those
// are the left and right arrows of the keyboard. That's exactly why they're
// there."): the text cut at the entry's INNER EDGES (its border and 8 W
// pads, kEntryPenW in from each side) and drawn through THE SCROLL OFFSET,
// the session's text_editor::State::view_offset_px — GtkEntry's
// adjust_scroll: the offset clamped to the run's own travel (none for a
// text shorter than the inner width, which stands at the left pen, offset
// 0), then moved only when the caret's x would leave the inner width, by
// just enough, so the caret is always in view (at the right edge it stands
// on the inner edge, its one W in the pad, as GTK draws it). The on-screen
// keyboard's LEFT / RIGHT and their hold-repeat walk the caret and so
// scroll the text; a tap seats the caret through the offset (the published
// origin, below). THE ENTRY NEVER MOVES OR GROWS WITH THE TEXT: its box is
// a fact of the view (modal_page_lines' `entry`).
// NO TITLE BAR, NO FRAME, NO SHADOW, NO OK, NO CANCEL, NO LIST BEHIND. THE
// ON-SCREEN KEYBOARD on the window's last rows (onscreen_keyboard.h's
// placement block) IS THE PAGE'S ONLY CONTROL: ENTER COMMITS AND ESC
// CANCELS, each through the editor's own key route
// (GuiInputHandler::handle_settings_editor_key: the commit's refusal
// selects the whole text and cards why, the page standing — A REFUSED ENTER
// RECOLOURS NOTHING, architect 2026-10-03; a commit or a cancel brings the
// list the page was opened from back, seated as the route seats it), and
// TAB KEEPS ITS COMPLETIONS, silent where it completes nothing
// (route_modal_editor_key). THE ENTRY'S TEXT ALONE IS LIVE BESIDE IT: the
// pen's tap and drag seat the caret on the published geometry below; a
// press anywhere else on the page presses nothing (the editor's veil,
// keyboard_modal_editor_active). THE TOP ICON ROW greys whole, the opener
// of the list the page was opened from — SETUP, PRESETS or BPM — lit and
// inert (roster_greyed_whole, roster_face_page_owner; app_state.h), fading
// as standing keys do (AppState::FaceFades); the label and the entry are no
// faces and fade nothing. THE BOTTOM ICON ROW AND THE STATUS BAR are under
// the keyboard (paint_bottom_strip's and paint_status_bar's yield).
// NO LINE EDITOR OPENS WHILE A QUESTION OR THE PROGRESS PAGE STANDS, AND
// NEITHER RISES OVER ONE: each of the three openers is a list's row or
// button (GuiInputHandler::settings_page_commit, trim_presets_save_as,
// bpm_form_commit), dead under a page — the page stands in place of the
// list's view and row and its gates consume every press and key — and the
// reverse is modal_page_stands' proof (app_state.h). So this page ranks
// last in paint_modal_dialog's fork and is never hidden by a higher rank
// while its keyboard stands.
//
// THE PAINT AND THE PUBLICATION for the line editor `ed`, on the view the
// caller has just laid: the label and the entry under the frame's own clip
// (the label reshaped only where the clip meets its line: an edit's and the
// blink's damage is the entry's box alone), and into app.modal_dialog the
// owner (Editor), NO BUTTON, and `field` — THE ENTRY'S BOX, border
// included: the caret's press region (active_editor_text,
// input_pointer.cpp) and the damage of an edit, a scroll step and the blink
// (Viewport::invalidate_dialog_editor_box) — and into
// app.dialog_editor_text the click-to-caret geometry of the run as painted.
// The caller stamps the session and `box`.
void paint_value_page(cairo_t* cr, AppState& app, text_editor::State* ed) {
    AppState::ModalDialogGeometry& dlg = app.modal_dialog;
    dlg.owner = AppState::ModalDialogOwner::Editor;
    const ModalPageLines l = modal_page_lines(app);
    if (l.view.w <= 0 || l.view.h <= 0 || l.entry.w <= 0) return;
    const GuiPalette& p    = palette();
    const GuiFont     body = gui_font(GuiFace::Body);
    const GuiLineBoxW lb   = gui_font_line_box_w(body);
    cairo_save(cr);
    cairo_rectangle(cr, l.view.x, l.view.y, l.view.w, l.view.h);
    cairo_clip(cr);
    // THE LABEL, the value's name and a colon, cut at the page margin
    // inside the view's right edge.
    const std::string& name = app.value_page.name;
    if (!name.empty() && clip_meets(cr, l.sentence_line)) {
        const double pen = static_cast<double>(l.pen_x);
        const text_raster::XSpan span{pen, static_cast<double>(l.text_end_x)};
        set_palette_source(cr, surface_text(GuiSurface::ListRow));
        text_shape::show_shaped_run_within(
            cr, text_shape::shape_text_run(body, name + ":"), pen,
            l.sentence_baseline, &span, 1);
    }
    // THE ENTRY, focused.
    const GuiRect entry    = l.entry;
    const int     pen_dx   = scaled_px(kEntryPenW);
    const int     caret_px = hairline_px();
    paint_entry(cr, entry, std::nullopt, /*focused=*/true);
    // THE SCROLL OFFSET (the head; GtkEntry's adjust_scroll): the inner
    // width, the run's travel past it, the session's last offset clamped to
    // that travel, then the caret brought inside [0, inner] by just enough.
    // Written back to the session, read by the next paint, reset by enter /
    // deactivate.
    const text_shape::ShapedRun run =
        text_shape::shape_text_run(body, ed->pending);
    const std::vector<double> bx_off =
        text_shape::byte_offsets_px(run, ed->pending.size());
    const int    cursor_pos = std::clamp(
        ed->cursor_pos, 0, static_cast<int>(ed->pending.size()));
    const double inner_x0 = static_cast<double>(entry.x + pen_dx);
    const double inner_w  =
        static_cast<double>(std::max(0, entry.w - 2 * pen_dx));
    const double travel   = std::max(0.0, run.width_px - inner_w);
    double off = std::clamp(ed->view_offset_px, 0.0, travel);
    const double caret_dx =
        bx_off[static_cast<std::size_t>(cursor_pos)] - off;
    if (caret_dx < 0.0)          off += caret_dx;
    else if (caret_dx > inner_w) off += caret_dx - inner_w;
    ed->view_offset_px = off;
    // PUBLISH the click-to-caret geometry (DialogEditorText's contract):
    // the window x byte 0 paints at, THROUGH THE OFFSET, and the run's
    // pen, so a tap and a caret drag land on the text painted here.
    const double tx = inner_x0 - off;
    AppState::DialogEditorText& out = app.dialog_editor_text;
    out.valid    = true;
    out.origin_x = tx;
    out.byte_x   = bx_off;
    const double baseline =
        line_box_baseline(body, static_cast<double>(entry.y),
                          static_cast<int>(kEntryHeightW));
    // The line box's rows: the selection's band and the caret's.
    const double band_y = baseline - static_cast<double>(scaled_px(lb.ascent));
    const double band_h = static_cast<double>(scaled_px(lb.line()));
    const auto byte_x = [&](int byte) {
        return tx + bx_off[static_cast<std::size_t>(byte)];
    };
    // The text and the selection, CUT AT THE INNER EDGES.
    const int b = key_border_px();
    cairo_save(cr);
    cairo_rectangle(cr, entry.x + pen_dx, entry.y + b,
                    std::max(0, entry.w - 2 * pen_dx), entry.h - 2 * b);
    cairo_clip(cr);
    const int n = static_cast<int>(ed->pending.size());
    if (text_editor::has_selection(*ed)) {
        const int a = text_editor::selection_start(*ed);
        const int e = text_editor::selection_end(*ed);
        const double hx0 = byte_x(a);
        const double hx1 = byte_x(e);
        paint_ink_rect(cr,
                       GuiRectF{hx0, band_y,
                                hx1 > hx0 ? hx1 - hx0 : 1.0, band_h},
                       p.selected_fill);
        set_palette_source(cr, p.field_text);
        text_shape::show_shaped_run_bytes(cr, run, tx, baseline, 0,
                                          static_cast<std::size_t>(a));
        text_shape::show_shaped_run_bytes(cr, run, tx, baseline,
                                          static_cast<std::size_t>(e),
                                          static_cast<std::size_t>(n));
        set_palette_source(cr, p.selected_text);
        text_shape::show_shaped_run_bytes(cr, run, tx, baseline,
                                          static_cast<std::size_t>(a),
                                          static_cast<std::size_t>(e));
    } else {
        set_palette_source(cr, p.field_text);
        text_shape::show_shaped_run_bytes(cr, run, tx, baseline, 0,
                                          static_cast<std::size_t>(n));
    }
    cairo_restore(cr);
    // THE CARET, one W wide from its byte's exact x over the line box's
    // rows, in the field's text, cut at the entry's padding box (one border
    // in) — at the inner right edge it stands in the pad, GtkEntry's seat;
    // shown on the blink's lit half (the entry always holds the focus), the
    // selection never gated.
    if (text_editor::cursor_visible_now(*ed)) {
        cairo_save(cr);
        cairo_rectangle(cr, entry.x + b, entry.y + b, entry.w - 2 * b,
                        entry.h - 2 * b);
        cairo_clip(cr);
        paint_ink_rect(cr,
                       GuiRectF{byte_x(cursor_pos), band_y,
                                static_cast<double>(caret_px), band_h},
                       p.field_text);
        cairo_restore(cr);
    }
    cairo_restore(cr);
    dlg.field = entry;
}

} // namespace

// THE RENDER PLAYER'S ROW LAYOUT (2026-10-21: lifted out of the modal
// painter's player branch): each of the eight cases' left
// column in the plan's order, the clock's two fields and the scrub's span,
// on the controls row's seats (modal_controls_seats). The row's arrangement
// and its rulings are at the painter's player branch. THE TRANSPORT STANDS
// FLUSH RIGHT AND EVERYTHING ELSE AT THE LEFT (architect 2026-11-10), the
// scrub and then the clock between the two clusters.
struct PlayerRowLayout {
    std::array<int, 8> x{};
    int field_w = 0, field_gap = 0;
    int scrub_x0 = 0, scrub_w = 0, clock_x0 = 0, clock_end = 0;
};
static PlayerRowLayout player_row_layout(const BottomRowSeats& seats) {
    PlayerRowLayout l;
    // THE CASES TOUCH (architect 2026-10-22, "the buttons should be
    // touching"): a run's keys are ONE LINKED GROUP, sharing one seam column
    // each (send Z, 2026-11-19; materials.h's key_link_pitch_px), as the
    // band's and the bottom row's keys; THE GROUP SPACE between its items is
    // the run gap (program_run_gap_px, the `run_gap` tuning key, 6 W its default;
    // architect 2026-11-03, mock dec7b,
    // and the retrofit's number; the head above the word rows). The press
    // reads the rects the button walk publishes from this layout
    // (modal_dialog_button_hit), the painted faces, so the ground between
    // two answers nothing; the scrub's span, its thumb's seat and its hit
    // rect are this layout's too.
    const int group    = program_run_gap_px();
    const int pitch    = key_link_pitch_px();

    // THE CLOCK'S TWO LABELS ARE MEASURED BEFORE ANYTHING IS PLACED,
    // their width being one of the fixed terms the scrub's own is what is
    // left over. Each is a TIME LABEL, plain text in its slot (the face and
    // the fixed-width rule at THE TIME FIELDS' block), the slot's width the
    // widest-digit specimen's advance rounded up to a whole W
    // (time_label_w_px), so neither label nor anything right of it moves as
    // the position runs or an item of another length loads — row 8's
    // timestamp's face, the program face (architect 2026-10-26 ~12:10,
    // "yes" to symmetry). THE ROW IS A GTK BOX AT 6 W SPACING (architect
    // 2026-11-18 / 19, send Z's design, built in send AA; his "it touches",
    // RULINGS §8): every pair of neighbors — the left group's ring, the
    // scale's trough, the position, the length, the transport's ring — one
    // run gap apart (the `run_gap` key, 6 W on the tablet).
    l.field_w   = time_label_w_px();
    l.field_gap = group;
    const int clock_w = l.field_w + l.field_gap + l.field_w;

    // -- The walk (architect 2026-11-10: the transport to the right,
    //    everything else to the left, the scrub and the clock where they
    //    stand between the two): THE LEFT CLUSTER, the five keys that are not
    //    the transport — the lamp, the highlight pair, Up a Folder and the
    //    Load in Place / Delete slot (no Close since 2026-11-11, architect,
    //    session 106: the lit PLAYER key is the way out, the scrub taking the
    //    seat's pitch) — one run from the row's
    //    first face; THE TRANSPORT'S THREE right-flushed where THE CONTROLS
    //    ROW'S FACES END, the last face's right edge the pane's last column
    //    (the well's; modal_controls_seats' cases_right); the scrub and the
    //    clock's two fields between them, the scrub taking what is left. --
    const int cx0 = seats.cases_left;
    for (int i = 0; i < 5; ++i)
        l.x[i] = cx0 + i * pitch;          // Repeat one .. Load in Place
    l.scrub_x0 = cx0 + linked_group_w_px(5) + group;

    const int transport_x0 =
        std::max(cx0, seats.cases_right - linked_group_w_px(3));
    // What the row owes AFTER the scrub, ahead of the transport: the group
    // space, the clock's two labels, the group space.
    const int after_scrub = group + clock_w + group;
    // THE FLOOR IS TWO KNOBS (2026-10-25, two thumbs then) — a trough
    // shorter than two of the knobs that ride it is not a scale. `scrub_w`
    // is THE TROUGH'S width; the knob's center travels half a knob inside
    // each of its ends (render.h's scrub block).
    l.scrub_w = transport_x0 - after_scrub - l.scrub_x0;
    if (l.scrub_w < 2 * scale_knob_px()) l.scrub_w = 0;

    l.clock_x0  = l.scrub_x0 + (l.scrub_w > 0 ? l.scrub_w + group : 0);
    l.clock_end = l.clock_x0 + clock_w;
    l.x[5] = transport_x0;                 // Home (skip back)
    l.x[6] = transport_x0 + pitch;         // Play / Pause
    l.x[7] = transport_x0 + 2 * pitch;     // End (skip forward)
    return l;
}

// THE SCORE VIEWER'S PAGE ROAD ROW LAYOUT (architect 2026-10-26,
// score_viewer.h's head, the face with no soundtrack): THE PLAYER'S SEATS
// (the transport flush right since 2026-11-10, architect) — RESET, START
// and END one linked group from the row's first column (no CLOSE since
// 2026-11-11, architect, session 106: the lit SCORE key is the way out, the
// scrub taking the seat's pitch), the page scrub
// after the group space, THE PAGE CELL where the player's clock stands, and
// PREV and NEXT one linked group flush right (the transport's seats), the
// last key's right edge the row's — the scrub taking what is
// left, floored at two knobs as the player's is, every neighbor one run gap
// from the next (the player's GTK box). THE CELL'S WIDTH is fixed for the
// table: a label's slot round the widest-digit specimen of
// "<count> / <count>" at the table's page count's digits, so nothing moves
// as the pages turn or the range changes.
struct ScoreRowLayout {
    std::array<int, 5> x{};
    int scrub_x0 = 0, scrub_w = 0;
    int cell_x0 = 0, cell_w = 0;
};
static ScoreRowLayout score_row_layout(const BottomRowSeats& seats,
                                       int page_count) {
    ScoreRowLayout l;
    const int group = program_run_gap_px();
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
    // THE PAGE CELL'S SLOT, A LABEL'S (send AA, 2026-11-19, the time
    // labels' rule): its specimen its own, "<count> / <count>" at the
    // widest digit, its advance rounded up to a whole W (label_w_px); its
    // run stays RIGHT-ALIGNED, its right end on the slot's right edge (the
    // painter's score branch), so "/ count" stands still as the pages turn.
    l.cell_w = label_w_px(GuiFace::Program, digits + " / " + digits);
    const int pitch = key_link_pitch_px();
    const int cx0 = seats.cases_left;
    for (int i = 0; i < 3; ++i) l.x[i] = cx0 + i * pitch;   // RESET .. END
    l.scrub_x0 = cx0 + linked_group_w_px(3) + group;
    const int pages_x0 =
        std::max(cx0, seats.cases_right - linked_group_w_px(2));
    l.scrub_w = pages_x0 - (group + l.cell_w + group) - l.scrub_x0;
    if (l.scrub_w < 2 * scale_knob_px()) l.scrub_w = 0;
    l.cell_x0 = l.scrub_x0 + (l.scrub_w > 0 ? l.scrub_w + group : 0);
    l.x[3] = pages_x0;                     // PREV
    l.x[4] = pages_x0 + pitch;             // NEXT
    return l;
}

// THE SCORE VIEWER'S MEDIA PLAYER ROW (architect 2026-10-26 night, the
// soundtrack's face, score_viewer.h's head; the transport flush right since
// 2026-11-10, architect): RESET, START and END one linked group from
// the row's first column (no CLOSE since 2026-11-11, the page road's
// reason), the scrub after the group space, THE
// CLOCK — the render player's two time
// labels, measured and spaced as its are (player_row_layout) — and BACK,
// PLAY / PAUSE and FWD one linked group flush right (the skips architect
// 2026-10-27, the render player's three transport seats), the scrub taking
// what is left, floored at two knobs as the player's is.
struct ScoreMediaRowLayout {
    std::array<int, 6> x{};
    int field_w = 0, field_gap = 0;
    int scrub_x0 = 0, scrub_w = 0, clock_x0 = 0, clock_end = 0;
};
static ScoreMediaRowLayout score_media_row_layout(const BottomRowSeats& seats) {
    ScoreMediaRowLayout l;
    const int group = program_run_gap_px();
    l.field_w   = time_label_w_px();
    l.field_gap = group;
    const int clock_w = l.field_w + l.field_gap + l.field_w;
    const int pitch = key_link_pitch_px();
    const int cx0 = seats.cases_left;
    for (int i = 0; i < 3; ++i) l.x[i] = cx0 + i * pitch;   // RESET .. END
    l.scrub_x0 = cx0 + linked_group_w_px(3) + group;
    const int transport_x0 =
        std::max(cx0, seats.cases_right - linked_group_w_px(3));
    l.scrub_w = transport_x0 - (group + clock_w + group) - l.scrub_x0;
    if (l.scrub_w < 2 * scale_knob_px()) l.scrub_w = 0;
    l.clock_x0  = l.scrub_x0 + (l.scrub_w > 0 ? l.scrub_w + group : 0);
    l.clock_end = l.clock_x0 + clock_w;
    l.x[3] = transport_x0;                 // BACK
    l.x[4] = transport_x0 + pitch;         // PLAY / PAUSE
    l.x[5] = transport_x0 + 2 * pitch;     // FWD
    return l;
}

// THE TRIM PRESETS LIST'S ROW LAYOUT (architect 2026-10-28 night, THE
// PLAYER'S SHAPE; his glass on 852a28d9: "up/down are in the bottom right for
// the player, so they should be there for the trim presets also"): ONE
// CLUSTER FLUSH RIGHT — THE HIGHLIGHT PAIR, the player's DOWN and UP keys,
// ONE LINKED GROUP (materials.h's linked_group_w_px), a run gap
// (program_run_gap_px), then the four WORD BUTTONS SAVE, SAVE AS, DELETE and
// CLOSE (modal_word_buttons: Arc's text buttons, one width, 4 W apart) — the
// last box's right edge the row's (every word row's alignment rule; the
// kit's presets row, send Z, 2026-11-19), nothing at the row's left. A
// DELIBERATE ASYMMETRY WITH THE PLAYER since 2026-11-10 (architect: the
// player's transport took its row's right, its pair and the rest the left;
// that ruling named the player and the score viewer alone): this list keeps
// its pair and its words in the one cluster flush right. THE BPM FORM'S ROW
// (architect 2026-10-29) is the same cluster with no pair: RENDER and
// CANCEL, two word buttons. `n_glyph` the pair's count (2, or 0), `words`
// the words; the answer each button's left column and width, in the plan's
// order (the pair first). On a row too narrow, the words start at the row's
// first column and the pair before them is cut off the row's left (the
// buttons stay whole; no window the product runs in reaches it).
struct KeyClusterLayout {
    std::vector<int> x, w;
};
static KeyClusterLayout key_cluster_row_layout(
        const BottomRowSeats& seats, int n_glyph,
        std::span<const std::string_view> words) {
    KeyClusterLayout l;
    const ModalWordButtons laid = modal_word_buttons(seats, words);
    const int words_x0 =
        laid.rect.empty() ? seats.cases_right + program_run_gap_px()
                          : laid.rect.front().x;
    const int pair_x0 =
        words_x0 - program_run_gap_px() - linked_group_w_px(n_glyph);
    for (int i = 0; i < n_glyph; ++i) {
        l.x.push_back(pair_x0 + i * key_link_pitch_px());
        l.w.push_back(icon_case_w_px());
    }
    for (const GuiRect& r : laid.rect) {
        l.x.push_back(r.x);
        l.w.push_back(r.w);
    }
    return l;
}

// THE TRIM PRESETS LIST'S HIGHLIGHT PAIR IS THE PLAYER'S (2026-10-28 night):
// each key's twin in the player's vocabulary, whose glyph
// (render_player_button_icon) and word (render_player_button_hint) it wears, so the pair reads the same on
// both rows.
static AppState::PlayerButtonAct trim_presets_pair_twin(
        AppState::TrimPresetsButtonAct act) {
    return act == AppState::TrimPresetsButtonAct::HighlightDown
               ? AppState::PlayerButtonAct::HighlightDown
               : AppState::PlayerButtonAct::HighlightUp;
}


// THE MODAL FAMILY'S BOX'S PAINTER (defined with the folder overlay's
// painter below, its other caller; `hole` the GL canvas's page hole and
// `pane` the interior's tone, the definition's head).
static void paint_modal_box(cairo_t* cr, const AppState& app,
                            const GuiRect& hole, GuiColor pane);

namespace {

// THE MODAL'S FACE STATE, dropped together: the press arm names a slot in
// modal_dialog.buttons, so it goes stale on exactly the edges that change
// the stash — and this painter owns every one of those edges (the rule and
// the edge list at AppState::modal_dialog_pressed). Its two companions, the
// inside bit and the press's clock, are meaningless without it and reset
// with it. IT IS IN HERE FOR A SHARPER REASON than a stale face: an arm is
// an act that has not happened yet, and a dialog that changed under it must
// not be able to receive it.
// No damage of its own: every caller is mid-frame on the lane it is about to
// repaint.
void reset_modal_dialog_face_state(AppState& app) {
    app.modal_dialog_pressed      = -1;
    app.modal_dialog_press_inside = false;
    app.modal_dialog_press_ms     = 0;
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
        AppState::PlayerButtonAct      player_act;
        AppState::ScoreButtonAct       score_act;
        AppState::TrimPresetsButtonAct trim_act;
        AppState::BpmFormButtonAct     bpm_act;
        bool                           enabled;
        bool                           lit;
    };
    std::vector<PrevFace> prev_enabled;
    prev_enabled.reserve(dlg.buttons.size());
    for (const AppState::ModalDialogButton& pb : dlg.buttons)
        prev_enabled.push_back(PrevFace{pb.player_act, pb.score_act,
                                        pb.trim_act, pb.bpm_act, pb.enabled,
                                        pb.lit});
    // THE OUTGOING THUMB CENTER AND TRACK, AS PAINTED, for the same reason:
    // a frame whose clip misses the track carries them (the scrub, below).
    const double  prev_thumb_x = dlg.scrub_thumb_x;
    const GuiRect prev_scrub   = dlg.scrub;
    dlg.valid   = false;
    dlg.owner   = AppState::ModalDialogOwner::None;
    dlg.session = 0;
    dlg.box     = GuiRect{0, 0, 0, 0};
    dlg.field   = GuiRect{0, 0, 0, 0};
    dlg.scrub   = GuiRect{0, 0, 0, 0};
    dlg.clock   = GuiRect{0, 0, 0, 0};
    dlg.scrub_thumb_x = -1.0;
    dlg.buttons.clear();
    app.dialog_editor_text = AppState::DialogEditorText{};

    // WHICH DIALOG: the prompt outranks every editor (the order the retired
    // status chain also kept; the close road takes every editor down before
    // the unsaved-work question rises, GuiPrompt::request_close, so the two
    // do not stand together).
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
    // neither live beside an editor. THIS IS ONE OF THE TWO PLACES THE
    // RANKING IS SPELLED (AppState::modal_dialog_live_session with
    // modal_dialog_live_owner beside it is the other) and they must agree or
    // the owner-tag doctrine breaks.
    const bool picker_up = !prompt_up && !progress_up && !player_up &&
                           app.picker.active;
    // THE SCORE VIEWER, the sixth owner (2026-10-26): the lower ranks' rank,
    // never live beside the player, the picker or an editor (no road opens
    // it under any of them — their routers and veils consume bare `m` and
    // grey its key — and its router consumes their openers).
    const bool score_up = !prompt_up && !progress_up && !player_up &&
                          !picker_up && app.score_viewer.active &&
                          app.score.has_value();
    // THE LINE EDITOR, the last rank — THE VALUE SUB-PAGE's (the settings
    // editor's buffer in its three kinds, AppState::dialog_editor_session's
    // set): none under a higher rank, none of which stands beside one (THE
    // VALUE SUB-PAGE's head). A MUTABLE STATE for the painter's sake: the
    // scroll offset it writes back is the session's.
    text_editor::State* ed =
        (prompt_up || progress_up || player_up || picker_up || score_up ||
         !text_editor::is_active(app.settings_editor))
            ? nullptr : &app.settings_editor;
    if (!prompt_up && !progress_up && !player_up && !picker_up &&
        !score_up && ed == nullptr) {
        // No dialog: the press arm resets WITH the stash, so a fresh dialog
        // cannot inherit the previous one's armed button.
        reset_modal_dialog_face_state(app);
        return;
    }

    // THE FACE STATE'S OTHER RESET EDGE, which keeps a dialog STANDING and so
    // never reaches the arm above (the whole rule is at
    // AppState::modal_dialog_pressed): A CHANGE OF SESSION. The index names
    // a button of a surface that is gone, and an arm carried across would be
    // aimed at whatever now sits at that slot. ONE test covers every such
    // edge because one raise takes one id: a prompt over an overlay, the
    // overlay back after it, a prompt REPLACING a prompt at the save-failed
    // rung (which no owner test can see), a list after its line editor.
    // Read before the branches below write anything.
    const uint64_t live_session = app.modal_dialog_live_session();
    const bool face_state_reset = prev_session != live_session;
    if (face_state_reset) {
        reset_modal_dialog_face_state(app);
    }

    // THE MODAL'S SURFACE (architect 2026-10-14, modal_box.h; send AA's
    // stage D2, 2026-11-19; send EE, 2026-11-20): THE OVERLAYS' controls
    // stand on THE CONTROLS ROW — the bottom icon row's rows under the box,
    // on the row's own ground, which paint_bottom_strip lays on every frame
    // class — and their lists are the folder overlay's painter's. A QUESTION
    // AND THE PROGRESS PAGE (`page_up`) ARE FULL PAGES: the box and its view
    // painted here, their lines on it, their answers this row's plan (THE
    // QUESTION PAGE AND THE PROGRESS PAGE, at the plan below). A LINE EDITOR
    // (`ed`) IS A SUB-PAGE (paint_value_page, THE VALUE SUB-PAGE above): the
    // box and its view painted here, its label and entry on it, NO ROW — the
    // on-screen keyboard its one control. THE BOX this body paints
    // (paint_modal_box): THE THREE PAGES' — the view in the lists' tone
    // whatever stood there, under a sub-page the on-screen keyboard over its
    // foot (modal_box.h's head; the cut below, send EE) — and THE SCORE
    // VIEWER'S (since 2026-10-26), its page the interior.
    //
    // THE CONTROLS ROW IS THE PAGE'S THAT STANDS IN THE VIEW (architect
    // 2026-11-19, send BB: "windows like score and player need the bottom
    // row, and so thy might as well keep the status bar, which sets the
    // precedent that overlays keep them"; his "i take your recommends for
    // all"; the rule's one owner modal_owns_bottom_row): THIS BODY PAINTS IT
    // WHILE A PAGE OR AN OVERLAY STANDS — a question's answers or the
    // progress page's Cancel (`page_up`: NO OVERLAY'S ROW IS PAINTED UNDER A
    // PAGE, the row flags below false, the overlay open underneath and its
    // row back when the page leaves) or the standing overlay's own row
    // (`row_player`, `row_list`, `row_score`) — the top icon row lighting
    // the same page's opener (roster_face_page_owner, app_state.h; under a
    // page greyed whole, the opener lit and inert, redesign_button_enabled's
    // first arm). UNDER A LINE EDITOR'S SUB-PAGE NO ROW IS PAINTED AND NO
    // BUTTON PUBLISHED: the on-screen keyboard stands over the row
    // (paint_bottom_strip's yield), the stash holds the entry alone.
    // THE CONTRACT AN OVERLAY UNDER A QUESTION RESTS ON: none of it is
    // painted, so no comparator is owed. The tick's four per-row face
    // comparators (main.cpp: the player's, the score viewer's, the trim
    // presets list's, the BPM form's) run only while modal_dialog.owner is
    // that overlay's; under a question the owner is Prompt and the row is
    // the question's answers, whose bits never move (every answer is live
    // while its question stands). The overlay's own row is repainted whole
    // when the page leaves (the leave damages the whole window).
    const bool    page_up  = prompt_up || progress_up;
    const bool    row_player   = player_up;
    const bool    row_list     = picker_up;
    const bool    row_score    = score_up;
    // THE ON-SCREEN KEYBOARD, standing over every line editor's sub-page on
    // the window's last rows (onscreen_keyboard.h's placement block,
    // architect 2026-11-20, send EE): everything this body lays is cut at
    // its top line (the clip below), the keyboard painted before this body
    // (on_redraw's order) and so never covered by it.
    const GuiRect kb_surf = onscreen_keyboard::stands(app)
                                ? onscreen_keyboard::surface_rect(app)
                                : GuiRect{0, 0, 0, 0};
    const bool    kb_covers = kb_surf.w > 0 && kb_surf.h > 0;
    // THE LIST'S PAGE THE ROW READS: the standing folder overlay.
    const AppState::FolderOverlay& row_page = app.folder_overlay;
    // THE TRIM PRESETS LIST (2026-10-28 night): a list whose controls row
    // is the player's shape, not the picker's one word.
    const bool    row_trim = row_list &&
                             row_page.owner ==
                                 AppState::FolderOverlay::Owner::TrimPresets;
    // THE BPM FORM (2026-10-29): a list whose controls row is two word keys
    // in the trim list's cluster, RENDER and CANCEL.
    const bool    row_bpm  = row_list &&
                             row_page.owner ==
                                 AppState::FolderOverlay::Owner::BpmForm;
    const GuiRect lane    = bottom_row_area(app);
    const GuiRect content = modal_controls_row_rect(app);
    if (lane.w <= 0 || lane.h <= 0 || content.h <= 0) {
        // A degenerate row publishes nothing, the cold answer the pointer path
        // already reads correctly (a zero stash contains no point) — and it
        // names no buttons, so the face state goes with it.
        reset_modal_dialog_face_state(app);
        return;
    }

    cairo_save(cr);
    // CUT AT THE KEYBOARD'S TOP LINE (the block at kb_covers above): the box
    // and its two lines. The cheaper truthful answer of the two — the
    // keyboard is painted before this body (on_redraw's order), so covering
    // would mean painting it twice. The sub-page's label and entry stand at
    // the view's head, clear of the surface at every tuning of the keys
    // (modal_box.h's head), so the cut takes nothing of them.
    if (kb_covers) {
        cairo_rectangle(cr, 0, 0, app.width, kb_surf.y);
        cairo_clip(cr);
    }

    // THE BOX, laid first. THE SCORE VIEWER'S INTERIOR IS THE FRAME'S HOLE
    // (architect 2026-10-27, the GPU page: the clip on_redraw's canvas block
    // chose at the frame's first rect, GuiCanvasPage::stands — the score
    // branch's own condition, score_viewer_page_stands), which the box
    // clears.
    GuiRect page_hole{0, 0, 0, 0};
    {
        const GuiCanvasDrawList& frame = gui_canvas_frame();
        assert(frame.page.stands == score_up);
        if (frame.page.stands) page_hole = frame.clip;
    }
    // THE VALUE SUB-PAGE is the line editor's as this body ranks it (`ed`:
    // none under a higher rank).
    const bool value_up  = ed != nullptr;
    const bool score_box = score_viewer_stands(app);
    // THE VIEW'S TONE `ruler_face`, the score viewer's `score_pane_face`
    // its one exception (modal_box.h's head, M9 A2) — NOT UNDER A PAGE: a
    // question over the viewer shows no mat, its page the lists' tone as
    // over every other page (the viewer's page hole is gone with its rank:
    // `score_up` is false under a prompt, so `page_hole` is empty there).
    // UNDER A PAGE THE BOX COVERS WHATEVER STOOD IN THE VIEW: the main
    // page's or the `h` view's lanes, which painted under it (on_redraw's
    // order: the lanes are not spared under the box), the canvas not painted
    // at all (onscreen_keyboard::waveform_paint_area, empty while the box
    // stands), an overlay's list not painted either (paint_folder_overlay
    // yields to the page).
    if (page_up || value_up || score_box)
        paint_modal_box(cr, app, page_hole,
                        score_box && !page_up ? palette().score_pane_face
                                              : palette().ruler_face);
    // THE PAGE'S LINES on the view just laid: the question, or the progress
    // page's sentence, bar and count (paint_modal_page).
    if (page_up) paint_modal_page(cr, app, prompt_up);
    // THE VALUE SUB-PAGE on the view just laid: the label and the entry,
    // and NOTHING ELSE OF THIS BODY — no controls row is planned, painted
    // or published under it (THE VALUE SUB-PAGE's head). THE STASH: the
    // owner and the entry's box are the page's own (paint_value_page); the
    // session is stamped here with them, and `box` is the modal family's
    // whole area, DAMAGE'S FALLBACK alone (the page is the view, and its
    // close gives the row back too).
    if (value_up) {
        paint_value_page(cr, app, ed);
        dlg.box     = modal_area_rect(app);
        dlg.session = live_session;
        dlg.valid   = true;
        cairo_restore(cr);
        return;
    }

    const BottomRowSeats seats = modal_controls_seats(app);
    // EVERY OWNER'S BUTTON IS THE KEY since 2026-10-23 (the word buttons
    // the keys, modal_word_buttons), so one band for all.
    const int btn_h = seats.case_h;

    // -- The buttons' words and widths, shaped up front (the layout needs the
    //    row's total before anything can be placed). --
    // A BUTTON IS A WORD OR A KEY (2026-08-28, the render player's row):
    // `glyph` set means the button is one of the player's keys, a key with
    // its act's glyph (render_player_button_icon) and its tooltip
    // (render_player_button_hint) — the transport
    // triple, the REPEAT ONE lamp, the HIGHLIGHT PAIR (architect 2026-10-10,
    // Highlight Next and Highlight Previous, the band walk's two keys on
    // glass), UP A FOLDER and LOAD IN PLACE (or DELETE at the root) —
    // while a word button is Arc's text button, its word alone on it and
    // its tooltip the word (send Z, 2026-11-19). The word kind is the
    // other owners' alone: a
    // prompt's answers, the progress page's Cancel and the picker's one
    // word (Cancel or Close by content, kModalCancelLabel's rule) — or, under
    // the trim presets list, its four words SAVE, SAVE AS, DELETE and
    // CLOSE after its two keys, the player's highlight pair, or under the
    // BPM form its two, RENDER and CANCEL. `lit` is
    // the roster's SELECTED face — the row's one lamp, Repeat one, and no other button has a state to say. `player_act` is the
    // player's dispatch vocabulary; the other two stay zero/false on its
    // buttons.
    struct DialogButtonPlan {
        std::string word;   // a word button's word: its glyph's key and its tooltip
        char        response_key = 0;
        AppState::PlayerButtonAct player_act = AppState::PlayerButtonAct::None;
        // The score viewer's dispatch vocabulary (2026-10-26); its keys are
        // `glyph` keys too, their glyphs score_viewer_button_icon's.
        AppState::ScoreButtonAct  score_act  = AppState::ScoreButtonAct::None;
        // The trim presets list's (2026-10-28 night): its highlight pair are
        // `glyph` keys wearing the player's glyphs, its other four word
        // buttons.
        AppState::TrimPresetsButtonAct trim_act =
            AppState::TrimPresetsButtonAct::None;
        // The BPM form's (2026-10-29): two word buttons.
        AppState::BpmFormButtonAct bpm_act = AppState::BpmFormButtonAct::None;
        bool        glyph        = false;
        bool        lit          = false;
        // The player's Play key's face (render_player_play_face), its
        // glyph's fork (2026-10-25); Play on every other button.
        PlayerPlayFace play_face = PlayerPlayFace::Play;
        // The disabled face (architect 2026-08-30, the player's row) — the
        // live predicate's answer at plan time, published on the record
        // below. FOUR OWNERS WRITE IT, re-grepped 2026-11-11: the
        // player's eight through render_player_button_enabled, the score
        // viewer's keys through score_viewer_button_enabled, the trim
        // presets list's six through trim_presets_button_enabled and the
        // BPM form's two through bpm_form_button_enabled; true on every
        // other owner's buttons.
        bool        enabled      = true;
        std::string tooltip;     // the player's own; empty = the composer's
        std::string tooltip2;    // the modifier line; empty = the one-line form
        int         w            = 0;
        // WHERE IT PAINTS. The single cluster every other owner
        // lays out is written into this field by the walk below; THE PLAYER'S
        // ROW IS NOT ONE CLUSTER (the lamp, the highlight pair, the Up a
        // Folder button and Load in Place at the left, then the scrub
        // and the clock, then the transport FLUSH RIGHT; architect
        // 2026-11-10), so its own branch writes all eight
        // and the walk simply paints where it is told.
        int         x            = 0;
    };
    std::vector<DialogButtonPlan> plan;
    // -- THE QUESTION PAGE AND THE PROGRESS PAGE (architect 2026-11-20, send
    //    EE — THE RULE'S ONE OWNER; his words: "we shuold remove the popups
    //    - it seems those come from gnome and xfce just inherited them (the
    //    oversized text is the clue). instead, the old prompt eg 'do you
    //    want to revert and drop changes?' should come back."; "yes,
    //    progress window can go back."; "i just want to make sure the dead
    //    buttons are somehow communicated." and, to "Arc's own disabled look
    //    is the dead keys' sign": "agree"; on the mocks, the question page
    //    TOP-ALIGNED ("question idem" after "settings prefer toplaigned"),
    //    "progress good"; to "blank the status bar while the progress page
    //    stands": "agree"; on the page margin: "1 agree").
    //    A QUESTION IS A FULL PAGE, AND THE PROGRESS PAGE IS THE SAME SHAPE.
    //    While one stands (modal_page_stands, app_state.h; the prompt ranks
    //    over the batch's progress, so a question raised mid-batch shows its
    //    page and the progress page is back when it leaves — or the render
    //    player, if the batch finished under it):
    //      THE VIEW (the box's interior, modal_box_interior_rect: on the
    //        tablet rows 142..1307, the panel's whole width) IS ONE FLAT
    //        PAGE in the lists' tone (`ruler_face`) WHATEVER STOOD THERE —
    //        the main page's trim bar, ruler, marker lane and canvas, the
    //        `h` view, an overlay's list, the score viewer's page on its mat
    //        (the box painted above, the head's block). ON IT THE LINES
    //        ALONE (paint_modal_page; modal_box.h's modal_page_lines, THE
    //        PAGE MARGIN): a question's one line of plain Regular 12 W in
    //        the lists' rows' tone, its pen 12 W from the view's left, its
    //        cap top 13 W under the view's top, cut 12 W inside the view's
    //        right edge, never wrapped; the progress page's "Rendering..."
    //        at that seat, its bar from 12 W inside the view's left to 12 W
    //        inside its right, 6 W tall, 6 W under the sentence's line box,
    //        and its count "3 of 11" (Regular 10 W) at the same pen, its
    //        line box 6 W under the bar.
    //      THE ANSWERS ARE ARC'S WORD BUTTONS WHERE THE BOTTOM ICON ROW'S
    //        KEYS STAND — THIS PLAN, the overlays' own row walk and no
    //        second painter: a question's answers in its table's order,
    //        Cancel last (AppState::PromptState::answers, each carrying its
    //        dispatch char as `response_key`), the progress page's one
    //        Cancel; laid by the word rows' one rule (modal_word_buttons on
    //        modal_controls_seats: max(85 W, the widest word + 34 W) each,
    //        4 W apart, flush right 6 W inside the panel — on the tablet OK
    //        x 1944..2113 and Cancel x 2122..2291 on rows 1324..1379), on
    //        the row's band in the body tone with NO timestamp, NO tab name,
    //        NO roster key and none of an overlay's own controls
    //        (modal_owns_bottom_row). EVERY ANSWER IS LIVE while its page
    //        stands (`enabled` true), painted and published by the walk
    //        below as every word button is: AT ONCE at its first paint (a
    //        new session's faces have no record), its hover and its press
    //        fading as Arc's sheet says on FaceFades::Surface::Dialog; the
    //        press arms on the published, current stash, the act at the
    //        lift (dispatch_modal_dialog_button). NO KEY REACHES EITHER PAGE
    //        and none has a focus.
    //      THE TOP ICON ROW GREYS WHOLE, a lit key lit and inert — TIPS,
    //        NOISE and a standing overlay's lit opener included
    //        (roster_greyed_whole, app_state.h) — fading as standing keys
    //        do (AppState::FaceFades).
    //      THE STATUS BAR under a question keeps its process line and drops
    //        its selection count; under the batch's progress it is BLANK
    //        (status_bar_text).
    //    OVER AN OVERLAY (the render player, the picker's contents, the
    //    trim presets, the score viewer) the page stands IN PLACE OF the
    //    overlay's view and of its controls row; the overlay stays open
    //    underneath, untouched, and is back as it stood when the page leaves
    //    (AN OVERLAY STAYS STANDING UNDER ITS OWN QUESTION,
    //    redesign_button_enabled's head).
    //    NEITHER PAGE EVER STANDS WITH THE ON-SCREEN KEYBOARD
    //    (modal_page_stands' head holds the proof), so nothing here is cut
    //    for it. NOTHING FLOATS: the box stands, the canvas is not painted
    //    under it, and the page is opaque chrome (canvas_sink.h's floaters
    //    are the cards and the tooltip).
    //    THE PAINTED GATE (AppState::PromptState): this branch is its ONE
    //    WRITER OF TRUE — the question's page and its answers go into the
    //    buffer paint_one_frame commits at the end of this same iteration,
    //    so from here on the prompt is a surface the user has seen and its
    //    buttons may answer. Every raise damages the whole window, so the
    //    frame that reaches here carries the page's pixels. A PROMPT
    //    REPLACING A PROMPT (the save-failed rung) is a new raise: `painted`
    //    false again and a new session, so the answers of the question it
    //    replaced — published on these very seats — answer nothing until
    //    this branch has painted the new one and the stash below names its
    //    session (modal_dialog_stash_current). --
    if (prompt_up) {
        app.prompt.painted = true;
        for (const PromptAnswer& a : app.prompt.answers) {
            DialogButtonPlan b;
            b.word         = a.word;
            b.response_key = a.key;
            plan.push_back(std::move(b));
        }
    } else if (progress_up) {
        // ONE Cancel (its one act a user owns is stopping the batch,
        // architect 2026-10-14; dispatch_modal_dialog_button's progress arm).
        DialogButtonPlan b;
        b.word = kModalCancelLabel;
        plan.push_back(std::move(b));
    } else if (row_player) {
        // THE PLAYER'S ROW, in painted order (architect 2026-08-28; THE
        // TRANSPORT TO THE RIGHT AND EVERYTHING ELSE TO THE LEFT, architect
        // 2026-11-10, each cluster keeping its own order): the REPEAT ONE
        // lamp, the HIGHLIGHT PAIR (Highlight Next, Highlight Previous: the
        // bare Down / Up keys' band walk on a button, architect 2026-10-10),
        // the UP A FOLDER button and Load in place from the row's
        // first face — then the scrub and the clock (both laid out in the
        // player's own branch below, the separators either side of them
        // retired 2026-10-02), then, FLUSH RIGHT, THE TRANSPORT (THE MAIN
        // WINDOW'S OWN TRIPLE since 2026-09-01): the two SKIPS around
        // Play/Pause, on Home and End since 2026-08-31.
        // NO CLOSE (architect 2026-11-11, session 106: "places like score
        // and the player can lose their close button since the button used
        // to enter that mode is also now the close button, and the tooltip
        // will say as much"): the lit PLAYER key is the way out
        // (PlayerButtonAct's head). No
        // key reaches the player (architect 2026-10-30), so the row has no
        // ring and no Tab walks it. A STOP BUTTON SAT AFTER PLAY/PAUSE from 2026-08-28
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
        // for the Load in place face and the play key's glyph.
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
        // THE PLAYER'S KEYS (`glyph` the key fork: a key with its act's
        // drawing, 2026-11-02; a word button carries its word alone).
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
        // THE LEFT CLUSTER (architect 2026-11-10), in its own order: the
        // lamp first.
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
        // more, so the way out of a batch folder is here — behind the pair,
        // ahead of the cluster's last key. It greys at the root through the
        // act's own wall.
        glyph_button(AppState::PlayerButtonAct::Up);
        // THE CLUSTER'S LAST KEY: Load in Place (the icon row's history
        // group runs the same act one surface over).
        // AT THE ROOT THE SLOT IS DELETE (architect 2026-09-29): `tmp/`'s own
        // listing is batch folders, where a load has nothing to name, so the
        // fifth button deletes the highlighted folder, and its shift press
        // every folder listed; its shift line stands only where "all" is more
        // than the highlighted one. Inside a batch folder the slot is Load in
        // Place, unchanged.
        hint.delete_all_differs = render_player_root_folder_count(app) > 1;
        if (app.render_player.folder == AppState::RenderPlayer::Folder::Root)
            glyph_button(AppState::PlayerButtonAct::Delete);
        else
            glyph_button(AppState::PlayerButtonAct::LoadInPlace);
        // THE TRANSPORT, FLUSH RIGHT (architect 2026-11-10), the row's last
        // three seats: Home, Play / Pause, End.
        glyph_button(AppState::PlayerButtonAct::Home);
        // PLAY/PAUSE: its glyph follows the play face (above); this is a
        // player's transport, where a pause is a resumable rest. The roster's
        // transport button is untouched — bare Space over the project's audio
        // is one toggle with no pause state.
        glyph_button(AppState::PlayerButtonAct::PlayPause);
        glyph_button(AppState::PlayerButtonAct::NextTrack);
    } else if (row_score) {
        // THE SCORE VIEWER'S ROW (architect 2026-10-26), in painted order, ON
        // ITS TWO FACES (score_viewer.h's head, 2026-10-26 night), THE
        // TRANSPORT TO THE RIGHT AND EVERYTHING ELSE TO THE LEFT as the
        // render player's row (architect 2026-11-10): RESET, START and END
        // from the row's first face — then (the scrub and the page
        // cell or the clock between, laid out in the branch below), flush
        // right, the page
        // road's PREV and NEXT, or the media player's BACK, PLAY / PAUSE and
        // FWD — a deck's order, the render player's transport triple (Home,
        // PlayPause, NextTrack above), the skips architect 2026-10-27, each
        // with its modifier line (score_viewer_button_shift_hint); PLAY's
        // glyph and its word the render player's Play key's by the face
        // (score_viewer_play_face) — each a key with its glyph, its face
        // the act's own truthful answer. NO CLOSE
        // (architect 2026-11-11, session 106, the player's ruling, its
        // branch above): the lit SCORE key is the way out.
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
        for (const Act act : {Act::Reset, Act::SetStart, Act::SetEnd})
            key(act);
        if (score_viewer_has_soundtrack(app)) {
            key(Act::SkipBack);
            key(Act::PlayPause);
            key(Act::SkipForward);
        } else {
            key(Act::PrevPage);
            key(Act::NextPage);
        }
    } else if (row_trim) {
        // THE TRIM PRESETS LIST'S ROW (architect 2026-10-28 night, THE
        // PLAYER'S SHAPE), in painted order, one cluster flush right
        // (key_cluster_row_layout): THE HIGHLIGHT PAIR — the player's DOWN
        // and UP, its glyphs and words, the band walk without applying
        // — then FOUR WORD BUTTONS, SAVE, SAVE AS, DELETE and CLOSE (the
        // Close word kModalCloseLabel's rule, app_state.h: the list has no
        // scrub, so it keeps its leave button, architect 2026-11-11, session
        // 107), CLOSE LAST. No Apply: the
        // row's tap is the act. Each face is the act's own truthful answer
        // (trim_presets_button_enabled).
        using Act = AppState::TrimPresetsButtonAct;
        for (const Act act : {Act::HighlightDown, Act::HighlightUp}) {
            DialogButtonPlan b;
            b.trim_act = act;
            b.glyph    = true;
            b.enabled  = trim_presets_button_enabled(app, act);
            b.tooltip  = render_player_button_hint(trim_presets_pair_twin(act),
                                                   RenderPlayerHintState{});
            plan.push_back(std::move(b));
        }
        const auto word = [&](Act act, std::string_view w) {
            DialogButtonPlan b;
            b.trim_act = act;
            b.word     = std::string(w);
            b.enabled  = trim_presets_button_enabled(app, act);
            plan.push_back(std::move(b));
        };
        word(Act::Save, "Save");
        word(Act::SaveAs, "Save As");
        word(Act::Delete, "Delete");
        word(Act::Close, kModalCloseLabel);
    } else if (row_bpm) {
        // THE BPM FORM'S ROW (architect 2026-10-29), in painted order, the
        // trim list's cluster flush right (key_cluster_row_layout): RENDER,
        // which greys until the three fields are set and low <= high, and
        // CANCEL (kModalCancelLabel's rule, the form asking for a choice),
        // CANCEL LAST. No OK: a row's tap is the edit, RENDER the answer.
        // Each face is the act's own truthful answer
        // (bpm_form_button_enabled).
        using Act = AppState::BpmFormButtonAct;
        const auto word = [&](Act act, std::string_view w) {
            DialogButtonPlan b;
            b.bpm_act = act;
            b.word    = std::string(w);
            b.enabled = bpm_form_button_enabled(app, act);
            plan.push_back(std::move(b));
        };
        word(Act::Render, "Render");
        word(Act::Cancel, kModalCancelLabel);
    } else {
        // THE PICKER'S ROW IS ONE BUTTON (architect 2026-08-29): a click on a
        // row opens it, so there is nothing for an OK to add. ITS WORD IS WHAT LEAVING DOES (the rule at
        // kModalCancelLabel, app_state.h, architect 2026-10-28 night): CANCEL
        // under the project picker and the value list, CLOSE under the
        // settings page (an overlay with no scrub keeps its leave button,
        // architect 2026-11-11, session 107: "Settings doesn't hurt to have
        // a close button and certainly looks better, I think, than having an
        // empty row") — the act the same body by content
        // (dispatch_modal_dialog_button's picker arm), the word alone moving.
        // (A question's answers and the progress page's Cancel are the
        // plan's first two branches; a line editor's sub-page has no button
        // and never reaches this plan.)
        DialogButtonPlan last;
        last.word = std::string(
            row_list && row_page.owner ==
                            AppState::FolderOverlay::Owner::SettingsPage
                ? kModalCloseLabel
                : kModalCancelLabel);
        plan.push_back(std::move(last));
    }
    // EVERY BUTTON'S BOX: a glyph button the PROGRAM'S CASE at row 8's width
    // (the player's eight and the score viewer's keys, placed by
    // its own branch below); every other owner's row — a question's answers
    // and the progress page's Cancel among them — ONE CLUSTER OF WORD
    // BUTTONS, laid out whole — widths, the flush-right start and each x —
    // by modal_word_buttons, the one layout the painter and the press read.
    if (row_player || row_score) {
        for (DialogButtonPlan& b : plan) b.w = seats.case_w;
    } else if (row_trim || row_bpm) {
        // THE TRIM PRESETS LIST'S ROW AND THE BPM FORM'S, laid out whole
        // here (no item in either for a branch below to place): the pair a
        // linked group, a run gap, the words Arc's text buttons, one cluster
        // flush right (key_cluster_row_layout).
        int n_glyph = 0;
        std::vector<std::string_view> words;
        for (const DialogButtonPlan& b : plan) {
            if (b.glyph) ++n_glyph;
            else words.push_back(b.word);
        }
        const KeyClusterLayout laid =
            key_cluster_row_layout(seats, n_glyph, words);
        for (std::size_t i = 0; i < plan.size(); ++i) {
            plan[i].x = laid.x[i];
            plan[i].w = laid.w[i];
        }
    } else {
        std::vector<std::string_view> words;
        for (const DialogButtonPlan& b : plan) words.push_back(b.word);
        const ModalWordButtons laid = modal_word_buttons(seats, words);
        for (std::size_t i = 0; i < plan.size(); ++i) {
            plan[i].x = laid.rect[i].x;
            plan[i].w = laid.rect[i].w;
        }
    }

    // -- THE CONTROLS ROW'S LAYOUT (architect 2026-10-14, modal_box.h) --
    //
    // THE ALIGNMENT RULE: the WORD BUTTONS SIT FLUSH RIGHT on every
    // row — a question's answers, the progress page's Cancel, the picker's
    // one word (Cancel or Close by content,
    // kModalCancelLabel's rule) and the trim presets list's four (its
    // highlight pair, the player's, directly before them in the one cluster)
    // and the BPM form's two — the last one's right edge 6 W inside the
    // panel (modal_controls_seats), today's button gap between them; the
    // render player's row and the score viewer's read from the left, their
    // other keys first and their transport flush right (architect
    // 2026-11-10; their branches below). (From 2026-08-29 the picker's list
    // kept its button flush right, "if I'm using the picker my hand stays on
    // the right".)
    //
    // THE BUTTONS STAY WHOLE: in the pathological case where they are wider
    // than the row, the cluster starts at the row's left seat and runs past
    // the right one (modal_word_buttons).
    //
    const int cx1 = seats.right_x;                      // one past the row's right seat
    // EVERY BUTTON IS CENTRED ON THE CASE ROWS (architect 2026-10-14, the
    // family's controls row): every button the program's key on the rows
    // the player's cases take, so the rows coincide
    // (modal_word_buttons). (Centered in the bottom row's content band, the
    // taskbar dialog's centered band before it, until that day.)
    const int btn_y = seats.case_y;

    // THE SCRUB'S ITEM — Arc's scale, painted and published (dlg.scrub,
    // dlg.scrub_thumb_x) — ONE BODY FOR ITS TWO OWNERS, the render player's
    // play-scrub and the score viewer's (2026-10-26, lifted out of the
    // player's branch): the trough at `scrub_x0` and `scrub_w` columns, the
    // item those columns by the knob's rows centered on the controls row's
    // keys (render.h's scale_item_rect), the knob's center at the real x
    // `thumb_x_of()` answers once the item is published (architect
    // 2026-11-15, the no-grid arc: the knob at its position's exact place,
    // render.h's scrub block). `thumb_pressed`: the owner's drag holds the
    // knob (its ScrubDrag armed on_thumb), its look Lit from the grab to the
    // lift — the whole disc fading toward the accent from the grab, and at
    // the lift back from the value it had reached (paint_scale; a press on
    // the trough carries the knob at rest; GuiInputHandler::hold_face). AT
    // REST UNDER THE HOVERING PEN its fill Arc's hover face; EVERY CHANGE OF
    // ITS LOOK FADES 200 ms, the held accent included (gtk3 CSS lines
    // 1144 .. 1150; send DD, 2026-11-20), the knob painted
    // as it stands on screen (AppState::FaceFades::face_as_painted, asked of
    // this row's own owner and session; render.h's scrub block). (A scrub's
    // row is never painted under a page: the player's and
    // the score viewer's rows stand only as the live surface.)
    const auto paint_scrub_item = [&](int scrub_x0, int scrub_w,
                                      bool thumb_pressed,
                                      const auto& thumb_x_of) {
            const GuiRect item =
                scale_item_rect(scrub_x0, scrub_w, GuiRect{0, btn_y, 0, btn_h});
            dlg.scrub = item;
            // THE KNOB'S CENTER — THE KNOB ALWAYS STANDS (2026-10-25,
            // render.h's scrub block) — is the owner's, asked once the item
            // is published (its mapping reads dlg.scrub): the player's at
            // its branch, the score viewer's at its own.
            const double hx = thumb_x_of();
            const AppState::FaceFades::Face knob{
                AppState::FaceFades::Surface::Knob, 0, dlg.owner,
                live_session};
            const FaceLook knob_look =
                thumb_pressed ? FaceLook::Lit
                : app.face_fades.hovered == knob ? FaceLook::Hover
                                                 : FaceLook::Rest;
            // The knob as painted (AppState::FaceFades): its record on no
            // box (the knob moves under a drag; its session names its
            // placement).
            const FaceMix knob_mix = app.face_fades.face_as_painted(
                knob, GuiRect{0, 0, 0, 0}, knob_look, clip_meets(cr, item));
            paint_scale(cr, item, hx, knob_mix);
            // THE CENTER IS PUBLISHED AS PAINTED (the field's rule,
            // AppState::ModalDialogGeometry::scrub_thumb_x), with no item
            // too: a clip that covers what the knob's move from the last
            // published center owes — its old and new discs and the trough
            // between (scale_knob_move_rect), or the whole item where there
            // is no last center on this item — left one knob, this one, on
            // screen; a clip that misses it left the last frame's knob
            // standing, so that frame's center is carried while the session
            // and the item are the same. A frame painted once per damage
            // clip reaches the publish because every damage that moves the
            // knob starts from this published center (the drag's motion, the
            // grab, the lift and the live tick's whole item; render.h's
            // scrub block, THE DAMAGE): the last move's clip covers the
            // whole owed move, so no run of clips each covering a part of it
            // leaves the center behind.
            const bool same_item =
                prev_scrub.x == item.x && prev_scrub.y == item.y &&
                prev_scrub.w == item.w && prev_scrub.h == item.h;
            const bool carried_ok =
                !face_state_reset && same_item && prev_thumb_x >= 0.0;
            const GuiRect owed =
                carried_ok ? scale_knob_move_rect(item, prev_thumb_x, hx)
                           : item;
            dlg.scrub_thumb_x =
                (carried_ok && !clip_covers_drawable(cr, app, owed))
                    ? prev_thumb_x
                    : hx;
    };

    if (prompt_up) {
        // A QUESTION'S ANSWERS (the plan's block): nothing else stands on
        // the row.
        dlg.owner = AppState::ModalDialogOwner::Prompt;
    } else if (progress_up) {
        // THE PROGRESS PAGE'S CANCEL, alone on the row.
        dlg.owner = AppState::ModalDialogOwner::Progress;
    } else if (row_player) {
        // -- THE RENDER PLAYER'S ROW (architect 2026-08-28 — the relaid
        //    row; THE TRANSPORT KEYS TO THE RIGHT, EVERYTHING ELSE TO THE
        //    LEFT, architect 2026-11-10, the scrub and the timestamps
        //    between them in their order): REPEAT ONE · DOWN · UP
        //    · UP A FOLDER · LOAD IN PLACE · SCRUB · CLOCK ·
        //    TRANSPORT, the transport flush right. The items are
        //    Audacious-Qt's under Breeze Dark, which is what the architect
        //    ruled off his own screen — "not the screenshot as a whole, just
        //    the layout: transport, separator, scrub, then the time, then
        //    another separator, then the repeat button" — and its TWO
        //    SEPARATORS RETIRED (architect 2026-10-02; Sound Recorder and
        //    mplay32 have none, and the roster's etched separators of
        //    2026-10-06 did not reach this row): the groups stand the run
        //    gap of bare face apart (program_run_gap_px, the `run_gap`
        //    tuning key, 6 W its default) — the left cluster → the trough,
        //    the trough → the position, the position → the length, the
        //    length → the transport, a GTK box at 6 W spacing (architect
        //    2026-11-18 / 19, send Z's design, built in send AA). THE
        //    CLOCK IS TWO TIME LABELS since
        //    2026-10-05 (architect: Sound Recorder's Position and Length
        //    panels, ACID's separate fields) — the position, then the
        //    length.
        //
        //    THE SCRUB IS THE ONE FLEXIBLE ITEM and everything else is fixed,
        //    so the row's primacy rule reads plainly here: THE BUTTONS STAY
        //    WHOLE AND THE SCRUB GIVES, and past its floor it PUBLISHES ZERO
        //    — the honest answer the tick and the press router already read
        //    (a zero rect contains no point and damages nothing) rather than a
        //    track too small to seat a handle.
        //
        //    THE PLAN IS EIGHT ITEMS — the left cluster's five (the lamp, THE
        //    HIGHLIGHT PAIR of 2026-10-10 behind it, Up a Folder of
        //    2026-09-01 and Load in Place; no Close since 2026-11-11) and the
        //    transport's three —
        //    every one of them a box of the same width and the whole row is fixed
        //    except the scrub. THE BOXES ARE ROW 8'S CASES (architect
        //    2026-10-05, bottom_row_seats; the program's since 2026-10-09):
        //    each cluster one linked group of Arc's buttons (the walk below)
        //    on the program's key at row 8's seat, the
        //    act's drawing centered on its box, the row on the panel's face (the
        //    player is the program's tenant of the row, paint_bottom_strip).
        //    The scrub is Arc's scale (render.h's scrub block); the two
        //    time labels are plain text in their slots (THE TIME FIELDS'
        //    block).
        //
        //    THE ROW IS THE MODAL FAMILY'S CONTROLS ROW (architect
        //    2026-10-14, modal_box.h; row 8's own from 2026-10-05): the
        //    buttons stand at row 8's case seat on row 8's rows, the
        //    left cluster's
        //    first face on the pane's first column (the well's, its cell on
        //    the box's outer column) and the right-flushed transport's last
        //    face ending on the pane's last (no
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

        // THE ROW'S LAYOUT is player_row_layout's (the runs of keys, the
        // group space, the clock's two fields measured first, the fixed left
        // cluster, the right-flushed transport and the scrub taking what is
        // between them —
        // the rules stated there). THE CLOCK'S FACE IS ROW 8'S CLOCK'S, THE
        // PROGRAM FACE (architect 2026-10-26 ~12:10, "yes" to symmetry; the
        // body face until then), the face its fields were measured in.
        const PlayerRowLayout pl = player_row_layout(seats);
        assert(plan.size() == pl.x.size());
        for (std::size_t i = 0; i < pl.x.size(); ++i) plan[i].x = pl.x[i];
        const int field_w   = pl.field_w;
        const int field_gap = pl.field_gap;
        const int scrub_x0  = pl.scrub_x0;
        const int scrub_w   = pl.scrub_w;
        const int clock_x0  = pl.clock_x0;
        const int clock_end = pl.clock_end;

        // -- THE PLAY-SCRUB, ARC'S SCALE (architect 2026-10-02; Arc's scale
        //    2026-11-18 / 19, send Z's design, built in send AA; the look and
        //    its owners at render.h's scrub block). The ITEM is published
        //    (dlg.scrub): the trough's columns by the knob's rows, so a
        //    press anywhere on the scale is on it and the per-position
        //    damage covers the knob's whole travel and the played part; the
        //    MAPPING (render_player_scrub_x_of) runs the knob's center, the
        //    frame's exact place, from half a knob inside the trough's left
        //    end to half a knob inside its right. --
        const AppState::RenderPlayer& rp = app.render_player;
        const int64_t pos = render_player_position(app, playback);
        if (scrub_w > 0) {
            paint_scrub_item(scrub_x0, scrub_w,
                             app.render_player.scrub.armed &&
                                 app.render_player.scrub.on_thumb,
                             [&]() -> double {
                // The drag's carried center while the thumb is being dragged
                // (the sound paused under the hold and the seek committing at
                // the release), the item position's own exact place
                // otherwise — the travel's start, frame 0, while no item is
                // loaded (render_player_scrub_x_of's frames <= 0 arm, the
                // clock's zeros).
                return rp.scrub.armed ? rp.scrub.marker_x
                                      : render_player_scrub_x_of(app, pos);
            });
        }

        // -- THE CLOCK, after the scrub (there since 2026-08-28): TWO TIME
        //    FIELDS since 2026-10-05, the POSITION and then the LENGTH, each
        //    plain text since 2026-11-19 (send AA) on the case's rows, its
        //    run's pen its slot's left edge, in the text tone
        //    (paint_time_text), the slots one run gap apart. ONE RECT ROUND
        //    BOTH IS PUBLISHED (dlg.clock) for the
        //    tick's per-position damage, beside the scrub's, and the two CLIP
        //    at the lane's right pad on a narrow window. WHILE THE SCRUB IS
        //    HELD THE POSITION FIELD SHOWS THE TIME THE DROP WOULD LAND AT
        //    (architect 2026-11-10): the lift's own derivation over the
        //    thumb's carried center (render_player_scrub_drop_frame,
        //    app_state.h), the item's position where the lift would seek
        //    nothing; the held scrub's edges damage dlg.clock
        //    (input_pointer.cpp's play-scrub block). --
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
            const int64_t at =
                rp.scrub.armed
                    ? render_player_scrub_drop_frame(app, rp.scrub,
                                                     rp.scrub.marker_x)
                          .value_or(pos)
                    : pos;
            const int64_t shown[2] = {at, rp.frames};
            for (int i = 0; i < 2; ++i) {
                const int field_x = clock_x0 + i * (field_w + field_gap);
                // PLAIN TEXT (THE TIME FIELDS' block): the run at its slot's
                // pen, kerning off, in the text tone (paint_time_text).
                paint_time_text(cr, time_label_rect(field_x, seats.case_y),
                                format_timestamp(seconds_of(shown[i])));
            }
            cairo_restore(cr);
            dlg.clock = GuiRect{clock_x0 - 1, content.y,
                                clock_right - clock_x0 + 2, content.h};
        }
    } else if (row_score) {
        dlg.owner = AppState::ModalDialogOwner::Score;
        const bool media = score_viewer_has_soundtrack(app);

        // THE PAGE (score_viewer.h's head, architect 2026-10-27, "1 a", THE
        // GPU PAGE): NOTHING OF IT IS PAINTED HERE. The interior is the
        // frame's hole, cleared by paint_modal_box above, and the GPU draws
        // the field and the page there at every swap from the gray store's
        // bytes (GuiCanvasPage, canvas_sink.h; the request published at the
        // frame's first rect, score_viewer_publish_gpu_page) — and
        // painted_page is the GL canvas's report of the page it put on
        // screen, which the tick reads (GuiScoreViewer::take_gpu_page_report).

        if (media) {
            // -- THE MEDIA PLAYER'S ROW (2026-10-26 night; the transport
            //    flush right since 2026-11-10): the three keys at the left,
            //    the scrub over the range's
            //    frames, the clock — the render
            //    player's items, painted by its own helpers and rules — and
            //    BACK, PLAY / PAUSE and FWD (2026-10-27) flush right. --
            const ScoreMediaRowLayout ml = score_media_row_layout(seats);
            assert(plan.size() == ml.x.size());
            for (std::size_t i = 0; i < ml.x.size(); ++i) plan[i].x = ml.x[i];
            const int64_t pos = score_viewer_position(app, playback);
            const ScoreFrameRange fr = score_viewer_frame_range(app);
            if (ml.scrub_w > 0) {
                paint_scrub_item(ml.scrub_x0, ml.scrub_w,
                                 app.score_viewer.scrub.armed &&
                                     app.score_viewer.scrub.on_thumb,
                                 [&]() -> double {
                    // The drag's carried center while the thumb is dragged,
                    // the cursor's own exact place otherwise.
                    return app.score_viewer.scrub.armed
                               ? app.score_viewer.scrub.marker_x
                               : score_scrub_x_of_frame(app, pos);
                });
            }
            // THE CLOCK, the render player's two time fields verbatim: the
            // elapsed time within the range, then the range's length, at
            // the soundtrack's own rate; WHILE THE SCRUB IS HELD the elapsed
            // field shows the time the drop would land at (architect
            // 2026-11-10; score_scrub_drop_frame, the lift's own derivation
            // over the carried center), the cursor's where the lift would
            // seek nothing.
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
                const AppState::ScoreViewer::ScrubDrag& sd =
                    app.score_viewer.scrub;
                const int64_t at =
                    sd.armed ? score_scrub_drop_frame(app, sd, sd.marker_x)
                                   .value_or(pos)
                             : pos;
                const int64_t shown[2] = {at - fr.begin, fr.length()};
                for (int i = 0; i < 2; ++i) {
                    const int field_x =
                        ml.clock_x0 + i * (ml.field_w + ml.field_gap);
                    // The player's text verbatim (paint_time_text).
                    paint_time_text(cr,
                                    time_label_rect(field_x, seats.case_y),
                                    format_timestamp(seconds_of(shown[i])));
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
            // the drag's carried center while the thumb is dragged (architect
            // 2026-10-27, "the thumb follows the pen continuously, the page
            // under it changes in steps"), the shown page's exact place
            // otherwise (score_scrub_x_of) — the step.
            if (sl.scrub_w > 0) {
                paint_scrub_item(sl.scrub_x0, sl.scrub_w,
                                 app.score_viewer.scrub.armed &&
                                     app.score_viewer.scrub.on_thumb,
                                 [&]() -> double {
                    return app.score_viewer.scrub.armed
                               ? app.score_viewer.scrub.marker_x
                               : score_scrub_x_of(app, app.score_viewer.page);
                });
            }

            // THE PAGE CELL where the player's clock stands: plain text
            // since 2026-11-19 (send AA, the time fields unboxed the same
            // way), reading "<ordinal> / <count>" — the shown page's ordinal
            // WITHIN THE RANGE and the range's count (the architect's "a page
            // number of its own") — A LABEL, RIGHT-ALIGNED, its run's right
            // end on the slot's right edge (its text is no timestamp and its
            // slot its own specimen's advance rounded up to a whole W,
            // score_row_layout), in the program face, the text tone.
            const int cell_right = std::min(sl.cell_x0 + sl.cell_w, cx1);
            if (cell_right > sl.cell_x0) {
                const ScoreViewerRange r = score_viewer_range(app);
                const std::string text =
                    std::to_string(app.score_viewer.page - r.first + 1) + " / " +
                    std::to_string(r.count());
                const GuiFont cfont = gui_font(GuiFace::Program);
                const GuiRect field{sl.cell_x0, seats.case_y, sl.cell_w,
                                    icon_case_h_px()};
                cairo_save(cr);
                cairo_rectangle(cr, sl.cell_x0, content.y,
                                cell_right - sl.cell_x0, content.h);
                cairo_clip(cr);
                const text_shape::ShapedRun run =
                    text_shape::shape_text_run(cfont, text);
                set_palette_source(cr, palette().text);
                text_shape::show_shaped_run(
                    cr, run,
                    static_cast<double>(sl.cell_x0 + sl.cell_w) -
                        run.width_px,
                    entry_baseline(cfont, field));
                cairo_restore(cr);
            }
        }
    } else if (row_list) {
        // -- THE PICKER'S ROW (architect 2026-08-28; ONE BUTTON since
        //    2026-08-29): **Cancel**, or **Close** under the settings page
        //    (kModalCancelLabel's rule, 2026-10-28 night), and NOTHING ELSE —
        //    no label, no field, no OK. The list in the
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
        //    ALONE, FLUSH RIGHT, ITS FACE ENDING ON THE PANE'S LAST COLUMN
        //    (x 2279 on the tablet, the well's last, the frame's; its cell on
        //    the box's outer column).
        //    THE TRIM PRESETS LIST'S ROW (2026-10-28 night) is the player's
        //    shape instead — the highlight pair and four words, one cluster
        //    flush right — laid out with the widths above
        //    (key_cluster_row_layout), and THE BPM FORM'S (2026-10-29) the
        //    same cluster of two, RENDER and CANCEL; nothing else stands on
        //    either for this branch to paint. --
        dlg.owner  = AppState::ModalDialogOwner::Picker;
    }

    // -- The button row. EVERY BUTTON PAINTS AT ITS OWN x (2026-08-28, the
    //    player's relaid row): the word cluster's x's and widths are
    //    modal_word_buttons' (key_cluster_row_layout's for the trim presets
    //    list and the BPM form), and the PLAYER's and the SCORE VIEWER's
    //    branches above have written all of their own — one walk, several
    //    arrangements, and no second painter. --
    //
    // EACH BUTTON'S STATE, ARC'S (send Z, 2026-11-19; materials.h's
    // key_state): LIT for the row's one checked button (Repeat One, `lit`)
    // and for a LIVE PRESS on any — armed with the pointer inside it — for
    // as long as the
    // press stands (Arc's `:active` and `:checked` one look); DISABLED on the
    // four rows that publish a stateful bit (AppState::ModalDialogButton::
    // enabled, the one statement of its owners: the PLAYER's buttons,
    // render_player_button_enabled; the SCORE VIEWER's keys,
    // score_viewer_button_enabled; the TRIM PRESETS LIST's pair and words,
    // trim_presets_button_enabled; the BPM FORM's two words,
    // bpm_form_button_enabled — every other owner's buttons, a question's
    // answers and the progress page's Cancel among them, are always live
    // while their surface stands); the press gated on the bit rather than
    // trusted — a button can go dead UNDER a hold. NO FOCUS LOOK AND NO
    // FOCUS (architect 2026-10-23: "when I open up the settings and I press
    // cancel, it gets that square drawn around it. Let's get rid of that";
    // no key reaches a row of buttons): the lit look under a standing press
    // is the only sign. AT
    // REST UNDER THE HOVERING PEN ARC'S HOVER FACE (send CC, 2026-11-20;
    // materials.h's THE HOVER): a resting button that is the hovered face
    // of this row's own owner and session is Hover (face_look). EACH
    // BUTTON AND EACH LINKED KEY'S LEFT SEAM IS PAINTED AS IT STANDS ON
    // SCREEN (send DD, 2026-11-20: "i'd like to see arc-truthful version
    // instead"; AppState::FaceFades, the one owner of the hover's walk and
    // gate and of every fade and its damage; the pointer walk here,
    // update_modal_dialog_hover, drives the tooltip and the press's inside
    // bit): face_as_painted and seam_as_painted off this run's looks — a key
    // this run is the first to paint in a new look (`plan[i].enabled` is the
    // live predicate, a press edge, a lit bit) begins its fade there and
    // paints the picture on screen, its seams in the same call.
    std::vector<FaceLook> looks(plan.size(), FaceLook::Rest);
    std::vector<FaceMix>  faces(plan.size(), FaceMix::of(FaceLook::Rest));
    std::vector<double>   seam_mixes(plan.size(), 0.0);
    const auto face_at = [&](size_t i) {
        return AppState::FaceFades::Face{AppState::FaceFades::Surface::Dialog,
                                         static_cast<int>(i), dlg.owner,
                                         live_session};
    };
    for (size_t i = 0; i < plan.size(); ++i) {
        const bool enabled = plan[i].enabled;
        const bool armed   = enabled &&
                             static_cast<int>(i) == app.modal_dialog_pressed;
        const bool pressed = armed && app.modal_dialog_press_inside;
        looks[i] = face_look(key_state(plan[i].lit, pressed, enabled),
                             app.face_fades.hovered == face_at(i));
    }
    // THE GLYPH KEYS ARE ARC'S LINKED BUTTONS AND THE WORD BUTTONS ARC'S
    // TEXT BUTTONS (send Z, 2026-11-19; materials.h): a run of glyph keys
    // standing one link pitch apart — the player's left cluster and its
    // transport, the score viewer's three and its pair or triple, the trim
    // presets list's highlight pair — is ONE LINKED GROUP (paint_linked_group,
    // each member's seam beside its box for the hit, linked_key_seam_px), so
    // a lone glyph key is a group of one; each word button is its own
    // bordered box (paint_word_button), its WORD ALONE centered on it — no
    // glyph (his ruling 2026-11-18) — in the Body face (Regular 12 W) on the
    // line-box baseline (+18 W), its state's ink (word_button_ink). The
    // drawings by the act vocabulary's own owner: the player's keys
    // render_player_button_icon (the Play key's naming its next press by
    // its face, 2026-10-25 — its repaint the row's one damage owner, the
    // play face's writers damaging the row whole), the score viewer's
    // score_viewer_button_icon (its PLAY / PAUSE the player's Play key's by
    // the face), the trim presets list's pair the player's twins'.
    std::vector<int> seams(plan.size(), 0);
    const auto glyph_of = [&](size_t i) -> std::optional<icons::Icon> {
        return plan[i].score_act == AppState::ScoreButtonAct::PlayPause
                   ? render_player_button_icon(
                         AppState::PlayerButtonAct::PlayPause,
                         plan[i].play_face)
               : plan[i].score_act != AppState::ScoreButtonAct::None
                   ? score_viewer_button_icon(plan[i].score_act)
               : plan[i].trim_act != AppState::TrimPresetsButtonAct::None
                   ? render_player_button_icon(
                         trim_presets_pair_twin(plan[i].trim_act),
                         PlayerPlayFace::Play)
                   : render_player_button_icon(plan[i].player_act,
                                               plan[i].play_face);
    };
    for (size_t i = 0; i < plan.size();) {
        if (!plan[i].glyph) {
            const GuiRect box{plan[i].x, btn_y, plan[i].w, btn_h};
            const bool exposed = clip_meets(cr, box);
            faces[i] = app.face_fades.face_as_painted(face_at(i), box,
                                                      looks[i], exposed);
            if (exposed) {
                paint_word_button(cr, box, faces[i]);
                const GuiFont wfont = gui_font(GuiFace::Body);
                const text_shape::SharedRun run =
                    text_shape::memoized_run(wfont, plan[i].word);
                cairo_save(cr);
                cairo_rectangle(cr, box.x, box.y, box.w, box.h);
                cairo_clip(cr);
                set_stamp_tone_source(cr, word_button_ink_tone(faces[i]));
                text_shape::show_shaped_run(
                    cr, *run,
                    static_cast<double>(box.x) +
                        (static_cast<double>(box.w) - run->width_px) * 0.5,
                    line_box_baseline(wfont, static_cast<double>(box.y),
                                      key_height_w()));
                cairo_restore(cr);
            }
            ++i;
            continue;
        }
        size_t j = i + 1;
        while (j < plan.size() && plan[j].glyph &&
               plan[j].x == plan[j - 1].x + key_link_pitch_px())
            ++j;
        const int n = static_cast<int>(j - i);
        for (size_t k = i; k < j; ++k) {
            seams[k] = linked_key_seam_px(static_cast<int>(k - i));
            // EACH KEY AND ITS LEFT SEAM AS PAINTED (AppState::FaceFades:
            // face_as_painted / seam_as_painted off this run's looks, the
            // fade of a look that moved begun in the same call).
            const GuiRect kbox =
                linked_key_box(plan[i].x, btn_y, static_cast<int>(k - i));
            const bool exposed = clip_meets(cr, kbox);
            faces[k] = app.face_fades.face_as_painted(face_at(k), kbox,
                                                      looks[k], exposed);
            if (k > i)
                seam_mixes[k] = app.face_fades.seam_as_painted(
                    face_at(k), kbox, looks[k - 1], looks[k], exposed);
        }
        const GuiRect group{plan[i].x, btn_y, linked_group_w_px(n), btn_h};
        if (clip_meets(cr, group)) {
            paint_linked_group(
                cr, plan[i].x, btn_y,
                std::span<const FaceMix>(faces.data() + i,
                                         static_cast<size_t>(n)),
                std::span<const double>(seam_mixes.data() + i,
                                        static_cast<size_t>(n)));
            for (size_t k = i; k < j; ++k)
                if (const std::optional<icons::Icon> icon = glyph_of(k))
                    paint_key_glyph(
                        cr, linked_key_box(plan[i].x, btn_y,
                                           static_cast<int>(k - i)),
                        *icon, key_ink_tone(faces[k]));
        }
        i = j;
    }

    for (size_t i = 0; i < plan.size(); ++i) {
        // THE PUBLISHED BOX: the key's box, border included ("the border is
        // clickable"), the hit box less its seam, the press damage and the
        // tooltip's seat.
        const GuiRect key{plan[i].x, btn_y, plan[i].w, btn_h};
        AppState::ModalDialogButton out;
        out.rect         = key;
        out.seam         = seams[i];
        out.response_key = plan[i].response_key;
        out.player_act   = plan[i].player_act;
        out.score_act    = plan[i].score_act;
        out.trim_act     = plan[i].trim_act;
        out.bpm_act      = plan[i].bpm_act;
        out.enabled      = plan[i].enabled;
        // The lit bit, as painted like the enabled bit below (the hover's
        // gate reads it: a lit key is unchanged under the hovering pen).
        out.lit          = plan[i].lit;
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
        // publish a constant true and need no gate. THE TRIM PRESETS LIST'S
        // ROW TAKES THE SAME GATE (2026-10-28 night): its faces move with the
        // highlight and the trim, whose acts damage the row, but a frame
        // whose clip missed a key must still not publish a bit it did not
        // paint. THE BPM FORM'S ROW TAKES IT TOO (2026-10-29), its RENDER
        // face moving with the fields. THE COVERAGE IS THE KEY'S BOX.
        if ((row_player || row_score || row_trim || row_bpm) &&
            !clip_covers_drawable(cr, app, key)) {
            const bool carry = !face_state_reset &&
                               i < prev_enabled.size() &&
                               prev_enabled[i].player_act == out.player_act &&
                               prev_enabled[i].score_act == out.score_act &&
                               prev_enabled[i].trim_act == out.trim_act &&
                               prev_enabled[i].bpm_act == out.bpm_act;
            out.enabled = carry && prev_enabled[i].enabled;
            if (carry) out.lit = prev_enabled[i].lit;
        }
        // THE HINT: a word button's is THE ACT'S NAME ALONE, its word
        // (architect 2026-10-10; no key behind it, the buttons being the
        // dialogs' one road, architect 2026-10-30); a glyph button's is the
        // player's own. Published with the rect because the WORD is
        // the half the pointer path cannot re-derive.
        out.tooltip = plan[i].tooltip.empty() ? plan[i].word
                                              : plan[i].tooltip;
        // THE MODIFIER LINE (2026-08-28), published beside it and empty on
        // every button with no shifted twin — the roster hint's `line2` over
        // this surface, its producers the player's plan and the score
        // viewer's (its skips, 2026-10-27) above.
        out.tooltip2 = plan[i].tooltip2;
        dlg.buttons.push_back(out);
    }

    // THE MODAL'S SURFACE: the score viewer's, a question's and the progress
    // page's the modal family's whole area (modal_area_rect — the box and
    // the controls row under it: the page is the view and its answers the
    // row), the player's and the picker's the controls row
    // (modal_controls_row_rect, the strip: pad + case + pad; the list above
    // it is the folder overlay's own rect) — because that is the rectangle
    // the modal owns and the rectangle its damage must erase (a line
    // editor's sub-page stamped its own above). DAMAGE'S FALLBACK
    // alone (AppState::ModalDialogGeometry's head): input reads the buttons.
    // THE SESSION IS STAMPED HERE, one write for both branches at the moment
    // the geometry becomes readable: it is the id of the surface these rects
    // belong to, and every input site that reads them compares it against the
    // live one (modal_dialog_stash_current, input_pointer.cpp).
    dlg.box     = row_score || page_up ? modal_area_rect(app) : content;
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
// THE KEYS ARE ONE GRID OF ARC'S BUTTON TONES WITH SHARED LINES (architect,
// his K2 ruling; send Z, his approval of tmp/sendZ/VIEW/Z_06; send AA's
// stage D2, 2026-11-19; the design's §E, the mock kit's keyboard the
// reference): the surface's TOP LINE `well_border` (Thunar's pane line
// parting the keyboard from what stands above) over THE BAND on the body
// tone `body_face`; in it THE GRID — every key row's outer rect
// (onscreen_keyboard::for_each_grid_row: its keys and the one-W line round
// them, neighbors sharing the line between them) in `well_border` (Arc's
// $borders_color, 2B2E39), THE GRID'S CONVEX OUTER CORNERS rounded
// kKeyCornerW (3 W) — the block's four, and a wider row's corner past a
// narrower neighbor's end (rows 3 and 4 are a unit wider than rows 0–2) —
// the concave ones square; each key's FACE `key_face` (444A58) on the walk's
// rect, the gap between faces the line, a face on a convex corner rounded
// one W less (key_inner_corner_px). EVERY EDGE AT EXACT COVERAGE through one
// coverage stamp pass (cool_edit_paint.h), the caps and the drawings after
// it. THE CAPS on their own device keys (`osk_key_w`, `osk_key_h`,
// `osk_font_px`, untouched): REGULAR in THE KEYS FACE (GuiFace::Keys, the
// `osk_font_px` tuning key's em in W, 12 by default; architect 2026-10-29
// night; Regular since this stage, the design's §E and Z_06, the Bold of
// 2026-11-19 before it), in `text` (D3DAE3), the cap band centered on the
// face (redesign_baseline); THE GLYPH KEYS (onscreen_keyboard::cap_icon,
// 2026-11-08) the program keys' drawing at its 16 W canvas centered
// (paint_key_glyph). ARC'S STATES, ONE LOOK FOR TWO (`:active` and
// `:checked` one in Arc): A LATCHED KEY — Shift while it is latched (sticky
// since 2026-11-08) and the page key while the symbols page stands, the two
// lamps — AND A KEY UNDER PRESS (held BY SLOT, so the page key's own page
// change keeps the face under the finger, same_slot) wear THE ACCENT `cue`
// under the lit print `text_on_lit` (FFFFFF); no key here is ever disabled.
// UNDER THE HOVERING PEN A RESTING KEY'S FACE IS ARC'S HOVER FACE (architect
// 2026-11-20, send CC, on the keyboard's keys: "Four, yes."; materials.h's
// THE HOVER), the face alone — the grid wears its lines always — the cap
// unchanged; a lit key is unchanged. EVERY CHANGE AMONG THE THREE LOOKS
// FADES AS ARC'S SHEET SAYS, the face and the cap's ink on one clock — a
// press or a latch 50 ms into the accent, a lift, an unlatch or a hover 200
// ms (architect 2026-11-20, send DD; AppState::FaceFades, the one owner;
// each key painted as it stands, face_as_painted); a cap's letter,
// shifted or not, changes at once, and the keyboard appearing or turning
// its page paints its keys at once. NO TOOLTIP: a hint has nowhere to hang
// here.
//
// A CAP IS A GLYPH, A WORD OR A CHARACTER (architect 2026-11-08, sessions
// 95 / 96, reversing "no glyphs" of 2026-08-27 / 2026-10-29): the function
// keys that act on the text wear their glyph (onscreen_keyboard::cap_icon
// names which); Shift, Esc, Space and the page key say their words, the
// character keys their characters (UTF-8 encoded through
// text_editor::encode_utf8, the editors' own encoder), on the one sans face
// through the one shaping chokepoint.
//
// STATE-AXIS: every key's state is decided ONCE, from the two lamps and the
// one held index this body reads at its head — compared as a SLOT, not an
// index (onscreen_keyboard::same_slot, architect 2026-10-29 night) — and
// the cap comes from the layout table's own derivations (cap_icon,
// cap_word, shifted_char). There is no second list of what a key looks
// like anywhere.
void GuiPaintHandler::paint_onscreen_keyboard(cairo_t* cr,
                                              const GuiRect& exposed) {
    // (The slot's as-painted bit is written by paint_keyboard_slot, the
    // dispatch that calls this — the keyboard shares its band with the folder
    // overlay since 2026-08-28 and one bit describes the slot.)
    const bool    standing = onscreen_keyboard::stands(app);
    const GuiRect surf     = onscreen_keyboard::surface_rect(app);
    if (!standing) return;
    if (surf.w <= 0 || surf.h <= 0) return;
    // A PARTIAL exposure still paints, and correctly — the clip bounds it. Only
    // the BIT above waits for a full one.
    if (!rects_intersect(exposed, surf)) return;

    // The keyboard's state, READ ONCE here so every key below is painted
    // against one answer. THIS BODY ONLY READS IT: the reset that follows a
    // change of the live editor session has its own owner
    // (onscreen_keyboard::reconcile_session), deliberately not called from
    // here — a painter may declare no damage.
    const AppState::OnscreenKeyboard& kb = app.onscreen_keyboard;
    const onscreen_keyboard::Page page          = kb.page;
    const bool                    shift_latched = kb.shift_latched;
    const int                     held          = kb.pressed_key;
    const GuiPalette& p = palette();

    cairo_save(cr);
    // THE TOP LINE AND THE BAND'S GROUND, whole pixels across the window.
    const GuiRect band = onscreen_keyboard::band_rect(app);
    paint_cell_rect(cr, surf, p.body_face);
    paint_cell_rect(cr, GuiRect{surf.x, surf.y, surf.w, surf.h - band.h},
                    p.well_border);

    // THE GRID'S ROWS, their convex corners (the head).
    struct GridRow { double x0, x1, y0, y1; };
    std::array<GridRow, onscreen_keyboard::kRowCount> rows{};
    onscreen_keyboard::for_each_grid_row(
        app, page, [&](int r, double x0, double x1, double y0, double y1) {
            rows[static_cast<std::size_t>(r)] = GridRow{x0, x1, y0, y1};
        });
    constexpr int kRows = onscreen_keyboard::kRowCount;
    struct Corners { bool tl, tr, bl, br; };
    std::array<Corners, kRows> convex{};
    for (int r = 0; r < kRows; ++r) {
        const GridRow& g = rows[static_cast<std::size_t>(r)];
        const bool first = r == 0, last = r == kRows - 1;
        convex[static_cast<std::size_t>(r)] = Corners{
            first,
            first || g.x1 > rows[static_cast<std::size_t>(r - 1)].x1,
            last,
            last || g.x1 > rows[static_cast<std::size_t>(r + 1)].x1};
    }
    // A RECT WITH ITS OWN FOUR CORNERS on the stamp, whose rounded rect
    // rounds a side's two corners alike: two halves overlapping by the
    // radius, each rounding its own pair, each covering the other's corner
    // zone whole — one tone, so the union is the shape.
    const auto corner_rect = [](CoverageStampPass& pass, double x0, double y0,
                                double x1, double y1, double rad,
                                const Corners& c, StampTone tone) {
        if (!c.tl && !c.tr && !c.bl && !c.br) {
            pass.rect(x0, y0, x1, y1, tone);
            return;
        }
        const double mid = (y0 + y1) * 0.5;
        pass.rounded_rect(x0, y0, x1, std::min(y1, mid + rad),
                          c.tl ? rad : 0.0, c.tr ? rad : 0.0, tone);
        pass.rounded_rect(x0, std::max(y0, mid - rad), x1, y1,
                          c.bl ? rad : 0.0, c.br ? rad : 0.0, tone);
    };
    const double R = static_cast<double>(key_corner_px());
    const double r_in = static_cast<double>(key_inner_corner_px());

    // Each key's state, read once.
    const auto key_lit = [&](uint32_t index, const onscreen_keyboard::KeyDef& k) {
        using Role = onscreen_keyboard::Role;
        const bool pressed =
            onscreen_keyboard::same_slot(static_cast<int>(index), held);
        const bool latched =
            (k.role == Role::Shift      && shift_latched) ||
            (k.role == Role::SymbolMode &&
             page == onscreen_keyboard::Page::Symbols);
        return pressed || latched;
    };
    // EACH KEY AS PAINTED (send DD, 2026-11-20: "i'd like to see
    // arc-truthful version instead"; AppState::FaceFades, the one owner):
    // its look — Lit pressed or latched, Hover the hovered face, else Rest —
    // and its FaceMix off face_as_painted on the key's hull, the fade of a
    // look that moved (a press, a latch, a lift) begun there; a key this
    // exposure misses changes nothing. Held per slot for the caps' walk, so
    // the face and its print read one mix.
    constexpr std::size_t kSlots = static_cast<std::size_t>(
        onscreen_keyboard::kRowCount * onscreen_keyboard::kMaxRowKeys);
    std::array<FaceMix, kSlots> key_mix{};
    const auto slot_of = [](uint32_t index) {
        return static_cast<std::size_t>(index -
                                        onscreen_keyboard::kStableCodeBase) %
               kSlots;
    };

    {
        CoverageStampPass pass;
        pass.begin(cr);
        for (int r = 0; r < kRows; ++r) {
            const GridRow& g = rows[static_cast<std::size_t>(r)];
            corner_rect(pass, g.x0, g.y0, g.x1, g.y1, R,
                        convex[static_cast<std::size_t>(r)], p.well_border);
        }
        // THE FACES, each on the walk's rect; a face on a convex corner of
        // its row rounded one W less (the first key's left corners, the
        // last key's right ones).
        onscreen_keyboard::for_each_key(
            app, page,
            [&](uint32_t index, const onscreen_keyboard::KeyDef& k,
                const GuiRectF& rr) {
                // PER-KEY EXPOSURE: a narrow damage crossing the band pays
                // for the keys it crosses alone (the key's hull).
                if (!rects_intersect(exposed, hull(rr))) return;
                // The key's row and column, its index's place in the table
                // (onscreen_keyboard::key_index).
                const int slot = static_cast<int>(
                    index - onscreen_keyboard::kStableCodeBase);
                const int row = (slot / onscreen_keyboard::kMaxRowKeys) %
                                onscreen_keyboard::kRowCount;
                const int col = slot % onscreen_keyboard::kMaxRowKeys;
                const int count = onscreen_keyboard::row_of(page, row).count;
                const Corners& rc = convex[static_cast<std::size_t>(row)];
                const Corners kc{rc.tl && col == 0, rc.tr && col == count - 1,
                                 rc.bl && col == 0, rc.br && col == count - 1};
                // The key's face as painted (above).
                const AppState::FaceFades::Face fface{
                    AppState::FaceFades::Surface::Keyboard,
                    static_cast<int>(index)};
                const FaceLook look =
                    key_lit(index, k)                     ? FaceLook::Lit
                    : app.face_fades.hovered == fface     ? FaceLook::Hover
                                                          : FaceLook::Rest;
                FaceMix& m = key_mix[slot_of(index)];
                m = app.face_fades.face_as_painted(fface, hull(rr), look,
                                                   true);
                corner_rect(pass, rr.x, rr.y, rr.x + rr.w, rr.y + rr.h, r_in,
                            kc, mix_looks(m, key_face_tone));
            });
        pass.end();
    }

    // THE CAPS AND THE DRAWINGS, after the stamp.
    const GuiFont font = gui_font(GuiFace::Keys);
    const GuiThemeTones& t = installed_theme().tones;
    onscreen_keyboard::for_each_key(
        app, page,
        [&](uint32_t index, const onscreen_keyboard::KeyDef& k,
            const GuiRectF& r) {
            if (!rects_intersect(exposed, hull(r))) return;
            // The print as painted: the key's mix of its looks' inks (the
            // keyboard's two, the text and the print on a lit face).
            const StampTone ink =
                mix_looks(key_mix[slot_of(index)], [&](FaceLook l) {
                    return hex(l == FaceLook::Lit ? t.text_on_lit : t.text);
                });
            if (const std::optional<icons::Icon> icon =
                    onscreen_keyboard::cap_icon(k)) {
                paint_key_glyph(cr, r, *icon, ink);
                return;
            }
            std::string one;
            const char* cap = onscreen_keyboard::cap_word(k, page);
            if (cap == nullptr) {
                one = text_editor::encode_utf8(
                    onscreen_keyboard::shifted_char(k.ch, shift_latched));
                cap = one.c_str();
            }
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, cap);
            set_stamp_tone_source(cr, ink);
            text_shape::show_shaped_run(cr, run,
                                        r.x + (r.w - run.width_px) * 0.5,
                                        redesign_baseline(font, r.y, r.h));
        });

    cairo_restore(cr);
}

// -- GuiPaintHandler::paint_keyboard_slot ----------------------------------

void GuiPaintHandler::paint_keyboard_slot(cairo_t* cr, const GuiRect& exposed) {
    // THE AS-PAINTED BITS, AND THEY DESCRIBE PIXELS — the roster publisher's
    // own rule (publish_button_face, above), taken for its own reason. The
    // slot comparator (onscreen_keyboard::reconcile_slot_standing) pays each
    // tenant's show and hide off the drift between its live answer and its
    // bit, and on_redraw runs ONCE PER DAMAGE
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
    // THE BAND IS EACH TENANT'S OWN, ONE BIT A TENANT (2026-08-28; one bit a
    // tenant since architect 2026-11-20, send EE — AppState::
    // keyboard_painted_standing): THE KEYBOARD'S is its surface, a fact of
    // the window's foot alone, the same rect on the show and the hide — held
    // whole by its edges' own damage (onscreen_keyboard::edge_damage_rect,
    // paid at reconcile_slot_standing); THE
    // OVERLAY'S is the MODAL FAMILY'S PANEL while it stands — the view bar's
    // top row through the controls row's top since 2026-10-14 (modal_box.h),
    // whatever its listing's length (its fixed height) — and, with it gone,
    // the slot's damage rect (slot_damage_rect, the modal family's area), its
    // hide's own damage, which is what has to erase it. So each bit
    // describes the pixels THIS frame may have written for its tenant.
    const bool keyboard = onscreen_keyboard::stands(app);
    const bool overlay  = folder_overlay::stands(app);
    const auto covers = [&](const GuiRect& band) {
        return band.w > 0 && band.h > 0 && exposed.x <= band.x &&
               exposed.y <= band.y &&
               exposed.x + exposed.w >= band.x + band.w &&
               exposed.y + exposed.h >= band.y + band.h;
    };
    if (covers(onscreen_keyboard::surface_rect(app)))
        app.keyboard_painted_standing = keyboard;
    if (covers(overlay ? folder_overlay::surface_rect(app)
                       : onscreen_keyboard::slot_damage_rect(app)))
        app.overlay_painted_standing = overlay;
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
// The shape and every rect are modal_box.h's: THE BOX'S TWO OUTER ROWS —
// the line under the top icon row and the line over the bottom icon row
// (under the on-screen keyboard while it stands, paint_modal_dialog's cut,
// send EE) — in the
// well's line tone `well_border` (Arc's $borders_color, Thunar's pane
// border; send AA's stage D2), and the interior ONE FLAT FILL in `pane`:
// THE VIEW's tone (`ruler_face`, Arc's $base_color) for the lists and the
// three pages, the score viewer's `score_pane_face` its one
// exception (the caller's answer, modal_box.h's head), no frame, no ring, no
// shadow. The controls row under the box stands on the bottom icon row's
// own rows, whose ground paint_bottom_strip lays (the row standing down
// to the overlay's row, modal_owns_bottom_row). Publishes nothing.
// THE GPU PAGE'S HOLE (architect 2026-10-27, "1 a"; canvas_sink.h's
// TRANSPARENCY INVARIANT): `hole`, the box's interior while the score
// viewer's page stands under the GL canvas (the frame's clip; empty
// otherwise), takes neither the lines nor the pane — both laid
// under one clip of the box less the hole — and is CLEARED to alpha 0,
// erasing what the lanes painted there under the box (on_redraw's paint
// order, step 12). So every CPU pixel inside the hole is 0 until a floater
// paints over it, and the GPU's field and page show there.
static void paint_modal_box(cairo_t* cr, const AppState& app,
                            const GuiRect& hole, GuiColor pane) {
    const GuiRect panel = modal_panel_rect(app);
    if (panel.w <= 0 || panel.h <= 0) return;
    const GuiRect interior = modal_box_interior_rect(app);
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
    // THE BOX'S TWO OUTER ROWS THE LINES, THE INTERIOR THE ONE FILL.
    paint_cell_rect(cr, panel, p.well_border);
    paint_cell_rect(cr, interior, pane);
    if (holed) cairo_restore(cr);
    cairo_restore(cr);
}

// -- THE LIST VIEW (paint_list_view) and GuiPaintHandler::paint_folder_overlay --
//
// THE LIST PANEL (2026-08-28; the modal family's box since 2026-10-14),
// ONE PAINTER FOR EVERY CONTENT —
// the render player's folders and wavs, the Open project picker's project
// folders, the settings page's `key=value` lines under its group headings
// (2026-10-28), the value list's values (2026-10-28 night; glyph-less
// rows, a value as the config spells it), the trim presets and the BPM
// form. Contract and gate are at the declaration; the geometry, the scroll
// clamp and the one row walk are at folder_overlay.h, which this body reads
// and never restates; the row table and every state bit are
// AppState::folder_overlay.
//
// THE VIEW IS THUNAR'S DETAILS LIST UNDER ARC (architect 2026-11-19, send Z:
// "use tunar", his approval of tmp/sendZ/VIEW/Z_04; send AA's stage D2; the
// design tmp/sendY/P2_design_overlays.md §B at 12 W text, the mock kit's
// list_row / group_heading / list_view the reference): THE VIEW the
// canvas's tone (`ruler_face`, Arc's $base_color 404552) between the box's
// two lines (paint_modal_box), edge to edge, NO SCROLLBAR, no row
// separator; the rows on folder_overlay.h's heights, and the faces:
//   a RESTING row         -> NO FILL: the view shows through, the name in
//                            the text tone `text`
//   the HIGHLIGHT         -> THE SELECTION PAIR across the view's whole
//                            width: the bar `selected_fill` (arc_dark the
//                            accent 5294E2, mixed at bright_mix) under the
//                            lit print `selected_text` (FFFFFF)
//   PRESSED               -> THE SAME LOOK: Thunar is single-click, a
//                            pressed row a selected one (the lighter press
//                            row of 2026-11-05, Live's SelectionBackground
//                            under SelectionForeground, retired with it)
//   an INERT row          -> never lit: a Disabled row's name in `text_dim`
//                            (7E838F), A GROUP HEADING Thunar's section
//                            header in the text tone, Bold 12 W
// NO HOVER FACE (architect 2026-10-02) AND NO FOCUS FRAME (architect
// 2026-10-23): the lit row is the only sign.
// THE ROW'S DRAWING AND NAME, ON THE PAGE MARGIN (architect 2026-11-20,
// send EE; modal_box.h's THE PAGE MARGIN the rule's owner, its 12 W pen
// kPageMarginPenW; the row and its drawing Thunar's at 9 pt, measured on
// his shot): EVERY ROW'S FIRST THING STANDS AT THE MARGIN'S PEN, 12 W from
// the view's left edge — a plain row's name, A GROUP HEADING'S NAME ON THE
// ROWS' OWN PEN ("yes align"), and in a listing whose rows wear drawings
// the drawing's canvas. A list row's drawing — Folder on a folder or
// project row, ViewMediaTrack, the double notes, on a wav row (SOURCE's
// drawing, architect 2026-11-02, R14 (4) / R15 (Q4)) — is the set's whole
// 16 W canvas (kListDrawingBoxW) at the pen across and +2 W down
// (kListDrawingYW), in the TEXT TONE on every face (the drawing unchanged
// under the highlight, as Thunar's), its name 3 W after the canvas
// (kListDrawingGapW, Thunar's: the pen 12 + 16 + 3 = 31 W); the name
// Regular 12 W (GuiFace::Body) at the margin's pen itself in a listing
// where no row wears a drawing (the settings page, the value lists, the
// trim presets, the BPM form: "all lists that don't have the player's
// icons hold an empty space for an icon that doesn't exist", architect
// 2026-10-28 night), a heading's name Bold 12 W (GuiFace::Bold) at that
// pen in every listing; each on its row's line-box baseline, +14 W of the
// 20 and +16 W of the 24 (folder_overlay::row_face / row_baseline_px, the
// seat the head pad reads too). NOTHING STANDS CLOSER THAN THE SAME 12 W
// TO THE VIEW'S RIGHT EDGE: a name and a heading are cut there on their
// own mask. THE LIT ROW'S BAR KEEPS THE VIEW'S WHOLE WIDTH. Every length a
// whole W through scaled_px (a composite the sum of its rounded parts);
// every seat at the row's real place under the real scroll offset (the
// no-grid arc, 2026-11-15); the first row's capitals 13 W under the view's
// top at scroll 0 by the list's head pad (folder_overlay::head_pad_px).
// TWO MARKS, ONE ROW EACH AND POSSIBLY THE SAME ROW: the HIGHLIGHT (the
// row Load in place and the highlight pair act on) and the TRANSPORT'S
// ITEM, which its row does not mark (architect 2026-10-23).
static constexpr double kListDrawingBoxW = 16.0;
static constexpr double kListDrawingYW   = 2.0;
static constexpr double kListDrawingGapW = 3.0;

// ONE LIST VIEW: the rows of `ov` in the band `band` under the scroll
// offset `scroll`, clipped to the band (whole pixels: a row the band cuts is
// cut on whole rows, its real edges blending over the view's opaque fill
// the caller laid), per row under the exposure; a row under a press
// lights. STATE-AXIS: the face is decided
// ONCE per row from the highlight and the press arm, the drawing from the
// row's kind alone.
static void paint_list_view(cairo_t* cr, const GuiRect& exposed,
                            const AppState::FolderOverlay& ov,
                            const GuiRect& band, double scroll) {
    if (band.w <= 0 || band.h <= 0) return;
    if (!rects_intersect(exposed, band)) return;
    cairo_save(cr);
    cairo_rectangle(cr, band.x, band.y, band.w, band.h);
    cairo_clip(cr);
    const GuiPalette&    pal = palette();
    const GuiThemeTones& t   = installed_theme().tones;
    // THE NAMES' COLUMN, ONE PER LISTING (the block above), derived from the
    // rows themselves.
    const bool wears_drawings = std::any_of(
        ov.rows.begin(), ov.rows.end(),
        [](const AppState::FolderOverlayRow& row) {
            return row.kind == AppState::FolderOverlayRow::Kind::Folder ||
                   row.kind == AppState::FolderOverlayRow::Kind::Wav;
        });
    const int margin      = page_margin_pen_px();
    const int drawing_box = scaled_px(kListDrawingBoxW, 1);
    const int name_dx =
        wears_drawings ? margin + drawing_box + scaled_px(kListDrawingGapW)
                       : margin;
    folder_overlay::for_each_row(
        ov, band, scroll,
        [&](int index, const AppState::FolderOverlayRow& row,
            const GuiRectF& r) {
            // PER-ROW EXPOSURE: a row costs a fill, a drawing and a shaped
            // run, so a narrow damage pays only for the rows it crosses —
            // asked of the row's whole pixels, its hull (a cull names pixels).
            const GuiRect r_hull = hull(r);
            if (!rects_intersect(exposed, r_hull)) return;
            if (!rects_intersect(band, r_hull)) return;
            const bool inert   = folder_overlay::row_kind_is_inert(row.kind);
            const AppState::FolderOverlayPress& press = ov.press;
            const bool pressed = press.armed && press.inside &&
                                 !press.scrolling && press.row == index;
            const bool lit = !inert && (index == ov.highlight_row || pressed);
            if (lit) paint_cell_rect(cr, r, pal.selected_fill);
            // THE ROW'S TEXT SEAT: its kind's face, its baseline the row's
            // real top plus the kind's depth (folder_overlay.h's one
            // definition, the head pad's too), and the right cut, the page
            // margin inside the view's right edge (the block above).
            const GuiFont font = gui_font(folder_overlay::row_face(row.kind));
            const double baseline =
                r.y + folder_overlay::row_baseline_px(row.kind);
            const double text_end = r.x + r.w - static_cast<double>(margin);
            if (row.kind == AppState::FolderOverlayRow::Kind::Group) {
                // THE HEADING: Bold 12 W in the text tone on the rows' pen,
                // its line-box baseline in the 24 W row.
                const text_shape::ShapedRun run =
                    text_shape::shape_text_run(font, row.name);
                const text_raster::XSpan head_span{r.x, text_end};
                set_palette_source(cr, pal.text);
                if (head_span.x1 > head_span.x0)
                    text_shape::show_shaped_run_within(
                        cr, run, r.x + static_cast<double>(margin), baseline,
                        &head_span, 1);
                return;
            }
            if (row.kind == AppState::FolderOverlayRow::Kind::Folder ||
                row.kind == AppState::FolderOverlayRow::Kind::Wav) {
                const icons::Icon glyph =
                    row.kind == AppState::FolderOverlayRow::Kind::Wav
                        ? icons::Icon::ViewMediaTrack
                        : icons::Icon::Folder;
                const icons::InkInBox fit =
                    icons::ink_in_box(glyph, drawing_box);
                if (fit.ink_px > 0)
                    icons::draw_glyph(
                        cr, glyph,
                        r.x + static_cast<double>(margin) + fit.left,
                        r.y + static_cast<double>(scaled_px(kListDrawingYW)) +
                            fit.top,
                        fit.ink_px, t.text);
            }
            // THE NAME, cut the page margin inside the view's right edge on
            // its own mask (text_shape::show_shaped_run_within: no clip per
            // row).
            const text_shape::ShapedRun run =
                text_shape::shape_text_run(font, row.name);
            const double pen = r.x + static_cast<double>(name_dx);
            const text_raster::XSpan name_span{pen, text_end};
            set_palette_source(cr, inert ? pal.text_dim
                                         : surface_text(lit ? GuiSurface::ListRowLit
                                                            : GuiSurface::ListRow));
            if (name_span.x1 > name_span.x0)
                text_shape::show_shaped_run_within(cr, run, pen, baseline,
                                                   &name_span, 1);
        });
    cairo_restore(cr);
}

void GuiPaintHandler::paint_folder_overlay(cairo_t* cr, const GuiRect& exposed) {
    if (!folder_overlay::stands(app)) return;
    // UNDER A QUESTION'S PAGE OR THE PROGRESS PAGE THE LIST IS NOT PAINTED
    // (architect 2026-11-20, send EE; paint_modal_dialog's THE QUESTION PAGE
    // AND THE PROGRESS PAGE; modal_page_stands the one predicate): the page
    // stands in place of the overlay's view — the box and the page's lines
    // are paint_modal_dialog's, laid after this slot — and the
    // overlay stays open underneath, its rows, highlight and scroll
    // untouched, repainted when the page leaves (the leave damages the whole
    // window). Its rows answer no press meanwhile: the prompt's gate ranks
    // above the band's claim (on_button_press), and no overlay stands under
    // the progress page (every batch is dispatched with its overlay closed,
    // AppState::ModalDialogOwner's block). The slot's standing bit is
    // the dispatch's, unmoved (paint_keyboard_slot: the overlay stands).
    if (modal_page_stands(app)) return;
    const GuiRect surf = folder_overlay::surface_rect(app);
    if (surf.w <= 0 || surf.h <= 0) return;
    if (!rects_intersect(exposed, surf)) return;
    cairo_save(cr);
    // THE MODAL FAMILY'S BOX (modal_box.h, architect 2026-10-14): its two
    // lines and THE VIEW (paint_modal_box), then the rows under the view's
    // clip (paint_list_view) — THE ROW WALK'S CLIP IS THE CONTENT RECT AND
    // row_at'S CONTAINMENT IS THE SAME RECT (folder_overlay.h's
    // content_rect, the box's interior), so paint and hit agree about every
    // pixel at any scroll offset.
    paint_modal_box(cr, app, GuiRect{0, 0, 0, 0}, palette().ruler_face);
    paint_list_view(cr, exposed, app.folder_overlay,
                    folder_overlay::content_rect(app),
                    app.folder_overlay.scroll_px);
    cairo_restore(cr);
}

// -- THE WELL'S FOOT ROWS -----------------------------------------------------
//
// ONE PAINTER for the rows from the lane's foot line (top lane 4) through
// the well's foot (bottom lane 2): THE BODY TONE across them wherever the
// canvas does not stand, and over it THE WELL'S LINES in those rows — the
// lane's foot line and the well's foot (paint_well_frame_rows). Edge to edge
// since 2026-11-19 (send Z: the frame's sides and margins retired), so on a
// full window the canvas covers every column between the two lines. It
// runs before render_canvas. THE HOLE (2026-10-13, canvas_sink.h's
// transparency invariant): the fill skips `hole`, the canvas's paint area,
// which on_redraw clears for the GPU.
namespace {
void paint_canvas_column_frame(cairo_t* cr, const AppState& app,
                               const GuiRect& hole) {
    const GuiRect top  = lane_foot_line_area(app);
    const GuiRect foot = well_foot_area(app);
    const int y1 = foot.y + foot.h;
    if (top.w <= 0 || y1 <= top.y) return;
    const GuiPalette& p = palette();
    cairo_save(cr);
    cairo_set_antialias(cr, CAIRO_ANTIALIAS_NONE);
    const GuiRect face{top.x, top.y, top.w, y1 - top.y};
    // THE HOLE IS LEFT ALONE WHOLE: the ground paints under one
    // clip of the rows less the hole, so nothing writes inside it (the
    // known-clear record counts on that, on_redraw).
    for_each_part_outside(face, hole, [&](const GuiRect& part) {
        cairo_rectangle(cr, part.x, part.y, part.w, part.h);
    });
    cairo_clip(cr);
    paint_cell_rect(cr, face, p.face);
    paint_well_frame_rows(cr, app, top.y, y1);
    cairo_restore(cr);
}
} // namespace

// -- THE AS-PAINTED FINGERPRINTS AND THE OWED CHECK (2026-10-13) ----------
//
// The contract is at GuiUpperRowsFingerprint (app_state.h): a viewport step
// damages the viewport's lanes and owes the rows above them, and row 8's
// clock cell under the zoom appliers, a CHECK; this is the check and the
// two functions it compares with.

// WHICH FLOATING SURFACE OWES THE ROWS THE CHECK SPARES — the upper rows or
// the clock cell — THEIR WHOLE REPAINT. Re-derived from on_redraw's paint
// sequence 2026-10-13 and again 2026-11-19 (send BB): past the lane
// painters, the ground and the canvas column (each inside its own rows), the
// passes that paint OUTSIDE their lane are the floating surfaces — the
// notification cards (from the window's top right over the title bar and the
// top icon row, notifyd's seat since send BB, notification_stack_bound),
// the tooltip (under a top-row key's face, above every other owner's,
// tooltip_box_rect). (The on-screen
// keyboard stands over the app's last rows, nothing relaid (send EE), the
// clock's row yielding under it, and the folder overlay and the modal family's box at
// the view bar's top row, directly under the band (modal_box.h,
// 2026-10-14) — a question's page and the progress page in it, their
// answers on the clock's row as an overlay's row is
// (GuiClockFingerprint::yields), a line editor's value sub-page in it too;
// the flag surface's blit over the top strip
// is transparent above the marker lane.)
// THE TERM (2026-10-13, per family and per owner since send BB, architect
// 2026-11-19: "ok to implement the fix you suggested for the power draw by
// card"; a struck card stands for minutes over exactly these rows, and the
// pen rests on PLAY / STOP through a FOLLOW play with TIPS lit): a floater
// whose pixels or publications a viewport step can change in the rows the
// check spares makes the check declare THE RECT THE ROUTES DECLARE — the
// window's top through the owed foot (damage_window_top_rows) — whole, so
// the floater is repainted there with the rows under it. THE REASON
// (94d92f3b): "a floater's own publications ride a clip that covers it" —
// the tooltip, the one floater it binds, publishes its rect and words as
// painted only under a clip that covers its extent (paint_shift_tooltip's
// PUBLISHED AS PAINTED), and a lanes-only rect cuts a box that crosses the
// lanes' top. THE ROWS THE CHECK SPARES are the window's top through the
// viewport lanes' top (viewport_lanes_top_y; rows 0..139 on the tablet) —
// the route already damaged the rest of its rect — and the clock cell
// (clock_invalidate_rect). A family leaves the term where no viewport step —
// a pan, a zoom, FOLLOW's scroll, every route that owes the rows — can
// change its pixels over those rows or its publications:
//   * THE CARDS LEAVE IT. A card's pixels are a function of the stack (its
//     words, class and order), the window, the scale, the palette and the
//     face (the fingerprint's globals, each a whole repaint), and of the
//     pixels under it. No viewport step writes the stack, and every write
//     of it damages the cards it changed itself (notify, dismiss,
//     dismiss_all, the clock and sync_struck_card, through
//     Viewport::invalidate_notification_stack; the re-word its own rect
//     too). Under a card in the upper rows lie the rows' own pixels: where
//     the check finds the fingerprint unmoved they and the card over them
//     are what a repaint would put there, and where it finds it moved it
//     declares the rows and the card repaints over them (paint_notifications
//     runs under every rect, on_redraw); a card's part over the viewport's
//     lanes repaints under the lanes' own damage the same way. Its
//     publication (AppState::Notifications::painted) is the layout's,
//     written whole on every run whatever the clip — it rides no covering
//     clip. The clock cell: the room ends a pad above the bottom icon row,
//     so no card stands over it.
//   * THE TOOLTIP, while visible, holds a check exactly where its box can
//     stand in that check's spared rows. ITS RECORD (AppState::
//     RedesignTooltip::rect) holds every pixel the box has standing — the
//     painted extent, the shadow's reach included (tooltip_painted_extent:
//     14 rows above the box, 18 below and 16 each side on the tablet), and a
//     box left behind where a clip missed it — so where the record misses
//     the spared rows no pixel of the tooltip stands there, and the check
//     owes it nothing there unless the step can carry the box in. Owner by
//     owner (the tablet's rows at key_height 28, the box 68 rows on one line
//     and 96 on two, the gap 2):
//       - A PENDING SHOW (show_pending) HOLDS BOTH CHECKS: nothing of the
//         box is painted yet, and where it lands is read at paint time off
//         publications the frame itself rewrites (a cue's face in the flag
//         stash on_redraw promotes, a dialog button in the modal stash
//         paint_modal_dialog republishes), so no pre-paint reading places
//         it; the first paint run that REACHES the box's layout puts the
//         bit out (paint_shift_tooltip, after the measure form's return).
//         A body that returns earlier — a Marker or History owner whose
//         words are empty, a Dialog owner whose stash is invalid or whose
//         owner or session no longer matches or whose button has no words,
//         a roster owner with no line 1, a face with no rect — leaves it
//         up, and this term then holds both checks until the walk hides
//         the box: the conservative answer.
//       - A RECORD STILL EMPTY (an owner whose body paints no box) HOLDS
//         BOTH: a box that has not painted has no place to test.
//       - A MARKER OR HISTORY OWNER HOLDS THE UPPER ROWS' CHECK WHATEVER ITS
//         RECORD: its box follows a cue a viewport step moves
//         (marker_cue_face, history_cue_face), so the step changes its
//         pixels wherever it hangs, and the flag rebuild's damage counts on
//         this check to reach the window's top for it (maybe_rebuild_flag_
//         cache's whole rect); a labeled cue's face starts on the marker
//         lane's first row (202, its label's box on the lane's rows
//         202..233), its box on rows 132..199 (two lines 104..199) and its
//         extent from row 118 (two lines 90), reaching into the spared rows
//         (118..139; 90..139), while a chip-only face's
//         starts lower — and whether a label paints moves with the step (a
//         later label's box covers it). Its box never nears the clock cell:
//         it hangs above the marker lane, the canvas between.
//       - A TOP-ROW ROSTER KEY (face rows 70..125): its box hangs from the
//         gap under the face, rows 128..195 (two lines 128..223), its extent
//         from row 114 — its record always meets the spared rows (114..139),
//         so it holds the upper rows' check.
//       - A BOTTOM-ROW ROSTER KEY (face rows 1324..1379): its box stands on
//         the gap over the face, rows 1254..1321 (two lines 1226..1321), its
//         extent 1240..1339 (two lines 1212..1339) — the whole canvas
//         column between it and the spared upper rows, so it never holds
//         that check; its shadow's
//         bottom rows (1322..1339) lie over the bottom icon row, so it holds
//         the clock cell's check where its record meets the cell.
//       - A DIALOG OWNER (a
//         question's answers, the progress page's Cancel, the render
//         player's, the score viewer's and the folder overlay's controls on
//         the bottom row): its seat is the modal stash's button, which no
//         viewport step moves (paint_modal_dialog republishes it from the
//         dialog's own state), so its record says where it can stand.
//     A ROSTER OR DIALOG BOX MOVES only with its face — a relayout, a row's
//     walk, a dialog's own layout, each with damage of its own — and changes
//     its words through its words comparator (main.cpp: the old record and
//     the band every seat of the new box lies in); a box that crosses
//     neither spared region was never covered whole by the check's rect, so
//     the term bought it no publication.
enum class OwedRows { Upper, ClockCell };
static bool tooltip_overlays_owed_rows(const AppState& app, OwedRows which,
                                       const GuiRect& spared) {
    const AppState::RedesignTooltip& t = app.redesign_tooltip;
    if (!t.visible) return false;
    if (t.show_pending) return true;
    if (t.rect.w <= 0 || t.rect.h <= 0) return true;
    if (which == OwedRows::Upper &&
        (t.owner.surface == AppState::RedesignTooltip::Surface::Marker ||
         t.owner.surface == AppState::RedesignTooltip::Surface::History))
        return true;
    return rects_intersect(t.rect, spared);
}

namespace {
// A hull's union: an empty rect adds nothing.
GuiRect hull_union(const GuiRect& a, const GuiRect& b) {
    if (b.w <= 0 || b.h <= 0) return a;
    if (a.w <= 0 || a.h <= 0) return b;
    return union_rect(a, b);
}
} // namespace

// THE FLOATERS' HULLS ARE THE FRAME'S OWN PUBLICATIONS (send BB, 2026-11-19;
// the list's field and its readers at canvas_sink.h's GuiCanvasDrawList::
// floaters): EMPTY AT THE FRAME'S FIRST RECT (on_redraw's canvas block) and
// WIDENED AFTER EACH on_redraw RUN, once its floating surfaces have painted
// (on_redraw's tail), by what each family published that run — every pixel
// it may have standing in the backbuffer:
//   * THE CARDS: the stack as this run laid it out, clipped to the room
//     (AppState::Notifications::painted_rect, written whole on every run
//     whatever the clip, paint_notifications);
//   * THE TOOLTIP: its record (AppState::RedesignTooltip::rect: the painted
//     extent, the shadow's reach included; united with the old box where a
//     clip missed it, and kept after a hide until a clip covering it has
//     repainted it — paint_shift_tooltip's head).
// (A question, the progress page and a line editor's value sub-page are no
// floaters: each stands on the modal family's box, under which the canvas
// is not painted — canvas_sink.h's THE FLOATERS' HULLS.)
// A publication lags a frame for a reader that runs BEFORE the paint that
// writes it (architect 2026-10-14: on the frame a floater appears such a
// reader sees nothing, and the floater's pixels over the canvas would be cut
// out of the upload); every reader of these hulls runs AFTER it — a later
// rect's narrowed clear, the GL canvas's upload and blend at the present, the
// next frame's fold into the known-clear record (canvas_clear_hulls) — so
// the lag does not reach them. THE FIRST RUN'S CLEAR NEEDS NO HULL OF THIS
// FRAME: outside the last painted frame's final hulls the area is known
// clear, so a floater arriving there paints over zeros, and a standing or
// moved floater's old pixels lie inside those hulls and are cleared before it
// repaints; a pixel an earlier run of this frame painted is in the hull that
// run's widening wrote, which every later run's clear reads.
void GuiPaintHandler::widen_floater_hulls(GuiFloaterHulls& h) {
    GuiRect& cards = h[static_cast<std::size_t>(GuiFloaterFamily::Cards)];
    cards = hull_union(cards, app.notifications.painted_rect);
    GuiRect& tip = h[static_cast<std::size_t>(GuiFloaterFamily::Tooltip)];
    tip = hull_union(tip, app.redesign_tooltip.rect);
}

// THE UPPER ROWS' LIVE FINGERPRINT — the ONE function the paint (publishing
// what it painted, note_upper_rows_painted) and the owed check (comparing,
// settle_owed_lane_damage) both call. EVERY INPUT THE ROWS' PIXELS READ,
// enumerated from their painters:
//   * the GROUND (render_background): the palette and the window's
//     geometry — the globals, with the scale every length and the one face
//     read;
//   * THE TITLE BAR (paint_title_bar, the band's first rows since
//     2026-11-19): the right group the tick composed (AppState::logo_legend);
//     the note, the brand, the well's columns and the lane's rect are the
//     globals' and the band's;
//   * THE TOOLBAR BAND (paint_icon_row): its lane (the pads among the
//     lane's inputs, its rect moving with it); the `h` view; the standing
//     members in the walk's order and their runs (icon_row_walk, the right
//     run after them); per member the enabled and
//     selected bits (publish_button_face's stash, refreshed by a covering
//     clip; the press face is the arm's inside bit, damaged by its own
//     edges, 2026-11-05), the glyph index
//     (redesign_button_glyph) and the drawing
//     (redesign_button_icon's stateful overload, 2026-11-02 — FIT / RECALL
//     and INHERIT / TIE move it with no index of their own); the cases'
//     tones are the palette's.
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
        m.glyph    = redesign_button_glyph(app, id);
        m.icon     = redesign_button_icon(app, audio, id);
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
// the row's body tone under the timestamp (the palette and the geometry,
// the globals), the cell as published (AppState::clock_cell_rect) and the
// run (clock_time_text: the scanner or the cursor, the sample rate) in the
// text tone; and whether the row yields, when the cell is not the clock's at
// all (modal_owns_bottom_row: an overlay's row, or a question's or the
// progress page's answers; or the on-screen
// keyboard standing over the row, architect 2026-11-20, send EE,
// onscreen_keyboard.h's placement block).
GuiClockFingerprint GuiPaintHandler::clock_fingerprint() {
    GuiClockFingerprint fp;
    fp.globals = GuiChromeGlobalsFingerprint{
        app.width, app.height, gui_scale_percent(), palette_generation()};
    fp.cell      = app.clock_cell_rect;
    fp.time_text = clock_time_text(app, audio);
    fp.yields    = modal_owns_bottom_row(app) || onscreen_keyboard::stands(app);
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
// invariant, kept by this rule), and the live glyph is the record's.
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
// seat that precedes every painted frame and may still add
// damage (paint_one_frame's rule): after the hook's own writes (the
// scanner's advance, Follow's scroll), and with nothing able to run between it
// and the paint. Each target that still owes is checked against what IT
// shows; any target whose record is invalid or differs from the live
// fingerprint, or the tooltip standing where it holds the check
// (tooltip_overlays_owed_rows: the one floating surface a viewport step can
// change over the spared rows, owner by owner), declares the rect the routes
// declare — the window top through the largest owed foot, the lanes below
// already damaged by the routes — and
// with it in every target's pending damage the debts are paid. A target
// that matches keeps nothing to repaint: its rows already show exactly what
// the repaint would put there. A frame whose present fails keeps its damage,
// the debts in it, for the next frame (paint_one_frame's tail).
// The clock cell the same, its rect clock_invalidate_rect's, a cell the row
// has not published (before the first paint, or under a yield) passing
// straight through as the lane.
void GuiPaintHandler::settle_owed_lane_damage() {
    static_assert(GuiPlatform::kPaintTargetCount <= kGuiPaintTargetSlots,
                  "one as-painted record per paint target");
    constexpr int n = GuiPlatform::kPaintTargetCount;
    // THE TOOLTIP ALONE OWES THE ROWS THEIR WHOLE REPAINT, each check where
    // its box can stand in that check's spared rows (the per-family and
    // per-owner proof at tooltip_overlays_owed_rows).
    int owed_y1 = 0;
    for (int t = 0; t < n; ++t)
        owed_y1 = std::max(owed_y1, app.upper_rows_painted[t].owed_y1);
    if (owed_y1 > 0) {
        const int top = std::min(viewport_lanes_top_y(app), app.height);
        bool stale = tooltip_overlays_owed_rows(
            app, OwedRows::Upper, GuiRect{0, 0, app.width, std::max(0, top)});
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
        bool stale = cell.w <= 0 || cell.h <= 0 ||
                     tooltip_overlays_owed_rows(app, OwedRows::ClockCell,
                                                clock_invalidate_rect(app));
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
    // value do nothing. A rebuild that stages a CHANGED pair always
    // invalidates its item region (every value of the pair is a field of
    // the flag cache's frame, whose miss declares the strip+waveform rect; a
    // content-only rebuild re-stages the standing pair, and promoting it
    // moves nothing — maybe_rebuild_flag_cache's tail, 2026-11-16), so the
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
    // loop iteration; before the scheduled frame paints a pointer press
    // arrives and arms pending_marker_press / pending_trim_drag; the frame
    // then runs this block and would
    // promote the new pair UNDER THE AIM — the press decided its subject and
    // stored its press_x against the OLD promoted basis, and the grab-gate
    // crossing (grab_moved_threshold_px)
    // would convert that stored column through the NEW one, which is exactly
    // the press-to-crossing epoch split the worker's dispatch freeze and
    // completion drop exist to prevent (they cover a job dispatched or
    // completing inside the freeze; they cannot reach a pair already staged
    // before the press). It is reachable: a pointer event and a due vsync land
    // in one loop pass's drain, the paint following at the pass's tail (pump,
    // platform_android.cpp), so "no input dispatches mid-paint" does not
    // exclude it.
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
    // the stage's original full-rect damage; the motionless lift disarms the
    // pending and damages nothing (its act ran at the press); and, the freeze
    // now being false, the next scanner-narrow frame promotes. The
    // trim bar and the flag editor's box would still show the old epoch while
    // item_viewport_basis and displayed_or_live_target_map answer on the new
    // one, so a pointer event in a later pass aims at a painted
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
        app.displayed_lattice_origin = app.staged_displayed_lattice_origin;
        app.staged_displayed_valid = false;
    }

    // THE UPPER ROWS' AS-PAINTED RECORD for the target this frame paints, and
    // its debts paid (note_upper_rows_painted; the contract at
    // GuiUpperRowsFingerprint, app_state.h) — before any pixel, on the state
    // every painter below reads.
    note_upper_rows_painted(GuiRect{x, y, w, h});

    // THE CANVAS IS THE GPU'S (2026-10-13; canvas_sink.h's head owns the
    // rule; the one road since 2026-10-30): the canvas's paint area — the
    // waveform area less the keyboard's slice, or nothing under the modal
    // family's box (onscreen_keyboard::waveform_paint_area), `canvas_paint`
    // — is left to the GPU: the chrome ground and the column's face skip it,
    // one CLEAR empties it, no canvas pass paints on the CPU, and the
    // frame's draw list is built instead — ONCE PER FRAME,
    // by the first damage rect that meets that area, WHOLE (never cut to the
    // rect) from the state every pass reads, the later rects of the frame
    // skipping it (the flags reset at the pre-paint hook,
    // begin_canvas_frame). The list's clip is written, and its floaters'
    // hulls emptied, on the frame's first rect whatever it meets (the hulls
    // then written after each run's floaters paint, widen_floater_hulls),
    // so the platform's scissor and upload rule follow a paint area that
    // shrank or emptied under no canvas damage (the keyboard rising, the
    // modal family's box standing).
    // THE SAME FIRST RECT PUBLISHES THE GPU PAGE'S REQUEST (architect
    // 2026-10-27, "1 a"; GuiCanvasPage, canvas_sink.h) and, while the score
    // viewer's page stands, makes THE BOX'S INTERIOR the clip: the canvas
    // paint area is empty then, the GPU draws the interior whole, and
    // paint_modal_box clears it — after the lanes that paint under the box —
    // so the known-clear record below stays the canvas's alone.
    const GuiRect exposed_rect{x, y, w, h};
    const GuiRect canvas_paint = onscreen_keyboard::waveform_paint_area(app);
    const bool canvas_paint_whole = canvas_paint.w > 0 && canvas_paint.h > 0;
    const bool plate_branch = audio.total_frames() > 0 && !app.loading;
    {
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
                frame.page.stands ? modal_box_interior_rect(app)
                                  : GuiRect{0, 0, 0, 0};
            assert(!(canvas_paint_whole && frame.page.stands));
            frame.clip = canvas_paint_whole ? canvas_paint : page_hole;
            // THE KNOWN-CLEAR RECORD'S FOLD AND THE FRAME'S DECISION (the
            // rule at the CLEAR below): the last painted frame's summary
            // becomes the record — the area clear OUTSIDE THAT FRAME'S
            // FLOATERS' HULLS as it finished them, which the list still
            // holds — and this frame narrows its clears to the hulls only on
            // a known-clear area it shares with that frame.
            if (canvas_clear_open) {
                canvas_clear_known = canvas_clear_narrow || canvas_clear_covered;
                canvas_clear_hulls = frame.floaters;
            }
            canvas_clear_open = true;
            // THE FLOATERS' HULLS, empty until this frame's runs publish
            // (widen_floater_hulls, this body's tail; the rule there).
            frame.floaters = GuiFloaterHulls{};
            gui.set_canvas_frame(&frame);
            canvas_clear_narrow = canvas_clear_known && canvas_paint_whole &&
                                  canvas_paint == canvas_clear_area;
            canvas_clear_area    = canvas_paint_whole ? canvas_paint
                                                      : GuiRect{0, 0, 0, 0};
            canvas_clear_covered = false;
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

    for_each_part_outside(exposed_rect, canvas_paint,
                          [&](const GuiRect& part) {
                              render_background(cr, part.x, part.y, part.w,
                                                part.h);
                          });
    // THE GROUND SPLIT: the chrome erase above covers the exposed rect less
    // the canvas's paint area; the well's lines then take their tone
    // (paint_canvas_column_frame, above), and the waveform area is the GPU's
    // (the block above): its `waveform_canvas` ground with the horizontal
    // grid over it (render_canvas, render.h's row-6 canvas paragraph) is the
    // list's first record, so a cold frame (loading, no audio, or no plate
    // before the first worker publish) shows canvas where the waveform will
    // be rather than a chrome-colored hole. THE CENTER LINES PAINT WHENEVER
    // THE GRID DOES and THE STEREO DIVIDER LIKEWISE (build_canvas_list's
    // `plate_branch`, render.h's row-6 canvas paragraph). The face skips the
    // canvas's paint area and one CLEAR empties it, so its pixels are alpha 0
    // until an overlay paints over them (canvas_sink.h's transparency
    // invariant).
    //
    // THE CLEAR OF AN ALREADY-CLEAR AREA IS SKIPPED (2026-10-14), OUTSIDE THE
    // FLOATERS' HULLS (send BB, 2026-11-19: a standing floater costs only
    // where it stands): the GUI keeps, in the paint handler and never from
    // the platform's upload state, whether the backbuffer's canvas paint
    // area is KNOWN CLEAR OUTSIDE THE LAST PAINTED FRAME'S HULLS
    // (canvas_clear_known, canvas_clear_hulls, paint_handler.h), and a frame
    // whose area is known so clears only where a rect meets a hull — of the
    // last frame (the pixels a floater left there) or of this one as it
    // stands at the rect (the pixels an earlier rect's floater laid, which
    // the floater repaints over the clear as it always did); everywhere else
    // its clears would write 0 on pixels already 0, so every byte is the
    // clearing frame's. With no hull meeting the area that is no clear at
    // all, exactly the skip of a frame with no floater.
    //   * KNOWN CLEAR OUTSIDE ITS HULLS after a painted frame whose area was
    //     clear at its end outside the hulls it finished with: a rect of it
    //     covered the area whole (its clear left every pixel 0, and only the
    //     floaters painted after it, inside their hulls), or the frame was
    //     narrowed (it began known so, and cleared every hull of the frame
    //     before; the pixels a departed floater leaves outside its new hull
    //     are damaged by its own leave — the cards' Viewport::
    //     invalidate_notification_stack, the tooltip's hide and words
    //     damage — and so cleared). Nothing
    //     else in a frame writes inside the area: THE PAINTERS OVER THE
    //     WELL, re-derived from the passes below (2026-10-14; 2026-11-20,
    //     send EE), are the cards (paint_notifications) and the
    //     tooltip (paint_shift_tooltip) — the two floater families, each
    //     inside its hull (the pull-downs one more until
    //     2026-10-21); the keyboard (paint_keyboard_slot) paints over the
    //     area's foot, which waveform_paint_area cuts off the area;
    //     the modal family's box — the folder overlay's
    //     (paint_keyboard_slot), a question's page, the progress page, a
    //     line editor's value sub-page and the score
    //     viewer's (paint_modal_dialog), with the keyboard over a value
    //     sub-page's foot — empties the area (nothing is cleared or skipped
    //     under it); the column's face and its two
    //     wells' rings above skip the area (its hole, a clip);
    //     every other pass keeps to its own lane (the band and row 8;
    //     paint_trim, the ruler and the flag blit in the top strip; the flag
    //     editor's field in the marker lane). Nothing writes the backbuffer
    //     between frames (the
    //     platform paints only through this pass).
    //   * UNKNOWN — the frame clears the area under every rect — when the area
    //     differs from the last painted frame's (the keyboard or the modal
    //     family's box came or went), at the first frame (a
    //     project set's paint handler starts unknown, the backbuffer being
    //     the process's), and after any on_resize (on_resize forgets the
    //     record): the backbuffer is remade only at a size it does not
    //     have, the platform firing the resize for it; a window adoption
    //     that moves nothing keeps the backbuffer and its bytes.
    {
        paint_canvas_column_frame(cr, app, canvas_paint);
        // THE WELL'S LINES OVER THE UPPER LANES ON A FRAME WITH NO AUDIO
        // (render.h's THE WELL'S LINES): their own painters (paint_trim,
        // paint_ruler_row) run in the plate branch alone, so here, where
        // render_background left those lanes the body tone, the lines' share
        // of the trim lane's top through the marker lane's foot is painted
        // here, and the lines stand whole in every frame class.
        if (!plate_branch) {
            const GuiRect trim_lane = top_trim_row_area(app);
            const GuiRect marker_lane = top_marker_row_area(app);
            paint_well_frame_rows(cr, app, trim_lane.y,
                                  marker_lane.y + marker_lane.h);
        }
        if (canvas_paint_whole) {
            if (!canvas_clear_narrow) {
                cairo_save(cr);
                cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
                cairo_rectangle(cr, canvas_paint.x, canvas_paint.y,
                                canvas_paint.w, canvas_paint.h);
                cairo_fill(cr);
                cairo_restore(cr);
            } else {
                // NARROWED: only the hulls' parts of the area this rect meets.
                const GuiFloaterHulls& now = gui_canvas_frame().floaters;
                bool any = false;
                const auto add = [&](const GuiRect& r) {
                    const int x0 = std::max({r.x, canvas_paint.x, x});
                    const int y0 = std::max({r.y, canvas_paint.y, y});
                    const int x1 = std::min({r.x + r.w,
                                             canvas_paint.x + canvas_paint.w,
                                             x + w});
                    const int y1 = std::min({r.y + r.h,
                                             canvas_paint.y + canvas_paint.h,
                                             y + h});
                    if (x1 <= x0 || y1 <= y0) return;
                    if (!any) {
                        cairo_save(cr);
                        cairo_set_operator(cr, CAIRO_OPERATOR_CLEAR);
                        any = true;
                    }
                    cairo_rectangle(cr, x0, y0, x1 - x0, y1 - y0);
                };
                for (const GuiRect& hl : canvas_clear_hulls) add(hl);
                for (const GuiRect& hn : now) add(hn);
                if (any) {
                    cairo_fill(cr);
                    cairo_restore(cr);
                }
            }
            if (x <= canvas_paint.x && y <= canvas_paint.y &&
                x + w >= canvas_paint.x + canvas_paint.w &&
                y + h >= canvas_paint.y + canvas_paint.h)
                canvas_clear_covered = true;
        }
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
    // sense — with no source the timestamp is empty and no tab is named
    // (the open's hold's one frame, platform.h's head). Nothing painted
    // later overlaps it except the floating surfaces, which paint over
    // everything by design.
    {
        const GuiRect exposed{x, y, w, h};
        // THE CHROME'S PAINT TIME (a throwaway trace for the planner,
        // 2026-10-21): one stderr line per full-window paint of the lanes
        // below, behind the system property `debug.warptempo.trace` == "1"
        // (`adb shell setprop debug.warptempo.trace 1` before a launch;
        // Android sets no environment for an app), read once, its stderr
        // logcat.
        static const bool trace = [] {
            char value[PROP_VALUE_MAX] = {};
            return __system_property_get("debug.warptempo.trace", value) > 0 &&
                   std::strcmp(value, "1") == 0;
        }();
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
        // THE STATUS BAR (send Z, 2026-11-19): audio-independent chrome on
        // the window's foot, painted wherever its lane is exposed but under
        // the on-screen keyboard (paint_status_bar).
        if (rects_intersect(exposed, status_bar_area(app))) {
            paint_status_bar(cr);
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

        // WHERE THE WAVEFORM'S PIXELS MAY LAND — `area` minus the modal
        // family's box or the on-screen keyboard's opaque surface, through
        // that rule's one owner (onscreen_keyboard::waveform_paint_area).
        // `area` itself is unchanged and stays what every pass below MAPS
        // with; this rect is what they are GATED on, and the clip below is
        // what keeps a pass that straddles a surface's edge from rasterizing
        // the part nobody sees. Equal to `area` while nothing stands over the
        // waveform.
        const GuiRect wave_paint = onscreen_keyboard::waveform_paint_area(app);
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
        // flag strips as published-then-overlaid layers. Trim is a live pass
        // (paint_trim) on the free item-basis owners.
        //
        // THE AUTHORITATIVE PAINT ORDER, bottom of the stack to top. This is
        // the ONE full enumeration in the tree — every other site states its
        // own pass's class plus a pointer here — and it is derived from the
        // call sequence below plus the two unconditional passes above this
        // branch. THE CANVAS INTERIOR'S SHARE of it (2026-10-13,
        // canvas_sink.h) — step 2's canvas and grid (and center lines and
        // divider without audio), step 4, steps 6 to 8 and step 11 — is the
        // frame's draw list in this same order, drawn by the GPU under
        // everything the CPU paints (build_canvas_list); the CPU's share
        // leaves that interior alpha 0:
        //   1. render_background — the chrome erase over the whole exposed
        //      rect (above, unconditional).
        //   2. paint_canvas_column_frame — the ground from the lane's foot
        //      line through the well's foot round the hole —
        //      then render_canvas — the waveform area's ground and
        //      the horizontal grid (above, unconditional; 2026-10-09; no
        //      vertical grid, render.h's row-6 canvas paragraph) — and, on a
        //      frame with no audio loaded (no source, or a load running:
        //      !plate_branch), THE CENTER LINES straight over it
        //      (render_center_lines), so the grid never stands without them,
        //      then THE STEREO DIVIDER'S ROWS (render_waveform_divider,
        //      2026-10-16).
        //   3. the toolbar band and the unified bottom row (its
        //      chrome, buttons, clock and the tab's name in one painter),
        //      and the status bar under it (paint_status_bar),
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
        //      divider — in the steady state no ink stands there, so the
        //      order changes no pixel; UNDER every vertical line, steps 4's
        //      ring through 11, each crossing it as any grid line, architect
        //      2026-11-02) -> phase-reset overlay ring.
        //   5. LIVE TRIM, one pass, entirely inside the trim lane: Cool
        //      Edit's view bar and its span (2026-10-09; no cursor mark in
        //      the bar, ~14:30).
        //   6. the CURSOR's CANVAS LINE (paint_playheads, solid, the
        //      palette's static `playhead_line` — the head is the ruler
        //      pass's, step 9).
        //   7. the MARKER STEMS (waveform): the selection stems, over the
        //      cursor's line (2026-11-05) — which is not drawn under a stem
        //      on its own exact place (paint_playheads' THE COINCIDENT
        //      LINES, architect 2026-11-14).
        //   8. the SCANNER (waveform), solid.
        //   9. the RULER lane — Cool Edit's flipped ruler, its ticks and
        //      digits — AND, in the same pass, the cursor's HEAD (the cyan
        //      triangle on the ruler's bottom rows, over the ticks and
        //      digits, its edge blended over them) and the MARKER LANE'S
        //      panel face under the cues.
        //  10. the FLAG BLIT — the cues.
        //  11. the strip-drag anchor stem (waveform, mid-gesture only), then
        //      THE SWEEP HIGHLIGHT (paint_sweep_highlight, architect
        //      2026-11-12; while a TRIM or ADD SEL sweep's pointer is down
        //      only), the canvas list's LAST record: a blend toward white
        //      over everything steps 2 through 11 put in the waveform area.
        //  12. the KEYBOARD SLOT (paint_keyboard_slot, outside this branch —
        //      the on-screen keyboard since 2026-08-27 or the folder overlay
        //      since 2026-08-28, one tenant at a time), whose opaque ground
        //      covers the waveform area's lower part, the well's foot, the
        //      bottom icon row and the status bar (the keyboard, on the
        //      window's last rows, nothing relaid, send EE, 2026-11-20 — so
        //      of step 3 the bottom row's and the status bar's tenants yield
        //      under it) or
        //      EVERYTHING FROM THE VIEW BAR'S TOP ROW TO ROW 8'S TOP (the
        //      overlay, the modal family's box since 2026-10-14, modal_box.h
        //      — so of steps 3 through 11 only the icon row's lane, the
        //      band greyed but for what it leaves live, and
        //      row 8's rows under the controls stay in view) and so follows
        //      every pass above.
        //      Which is why the WAVEFORM passes do
        //      not paint there at all: the list's clip is the waveform's
        //      PAINTED rect (onscreen_keyboard::waveform_paint_area, read
        //      just above this block), which the overlay's band reduces to a
        //      ZERO-HEIGHT rect
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
        //      2026-10-14 here: a question's page, the progress page, a
        //      line editor's value sub-page and the score viewer's
        //      box over everything from the line under the top icon row
        //      down, which the waveform's painted rect leaves out, the score
        //      viewer's interior CLEARED there, the GPU's page hole,
        //      2026-10-27; the controls row — the standing overlay's, live,
        //      or a page's answers (send BB, 2026-11-19; send EE,
        //      2026-11-20), none under a value sub-page), then
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
        //     colors, it never washes over them — WITH ONE EXCEPTION, THE
        //     SWEEP HIGHLIGHT (architect 2026-11-12: "an alpha overlay ...
        //     a blend towards white"), which washes the swept span toward
        //     white over every canvas pass while the sweep's pointer is down
        //     (step 11); no other pass recolours the waveform. The phase-reset
        //     overlay contributes no ground (architect 2026-07-27): its
        //     RING is its whole visual — four solid one-W lines in the
        //     `cue_outline` (2026-11-04) — painted AFTER the plate, over the
        //     ink like the stems, and before them.
        //   THE COLUMN IS SHARED, NOT CONTESTED (architect 2026-10-09, the
        //     program is Cool Edit): a SELECTION's mark is its members'
        //     stems (architect 2026-11-05), with the landed cursor on the
        //     focus; a selected marker the cursor stands on shows its solid
        //     stem in the cursor's line's place (2026-11-04; the line under
        //     it not drawn, 2026-11-14), the ruler's
        //     head marking the playhead; its head in the ruler and the cue's
        //     chip in the marker lane never meet. (2026-08-01 lifted the SCANNER above the
        //     stems, so the moving line does not blink out at every marker it
        //     crosses.)

        // THE CANVAS PASSES ARE NOT PAINTED HERE: the frame's list carries
        // them, built once, whole, in this order (build_canvas_list) — the
        // plate, the center lines, the stereo divider, the lead-in ring, the
        // cursor's line, the selection stems, the scanner, the zoom anchor,
        // the sweep highlight.

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

        if (rects_intersect(exposed, top_strip)) {
            // The ruler paints BEFORE the flags: it lays the marker lane's
            // panel face, which the flag blit's cues stand on.
            paint_ruler_row(cr);
            paint_flag_annotations(cr, top_strip);
            // The selection stem's head segment, over the blitted chips
            // (paint_marker_stem_heads; the rule at paint_marker_stems).
            paint_marker_stem_heads(cr);
        }

        if (band_cuts) cairo_restore(cr);
    }

    // THE KEYBOARD SLOT (the on-screen keyboard since 2026-08-27, the folder
    // overlay since 2026-08-28 — one band, one tenant at a time), between the
    // waveform passes above and the two floating surfaces below (the cards
    // and the tooltip) — which is exactly where it sits in the picture. The overlay OVERLAYS the waveform
    // with its own opaque ground — the MODAL FAMILY'S PANEL since 2026-10-14
    // (the view bar's top row down to the controls row, modal_box.h) — and
    // the keyboard OVERLAYS the canvas's foot, the well's foot, the bottom
    // icon row and the status bar on the window's last rows (send EE,
    // 2026-11-20), so the slot must follow every pass that paints there
    // (the top button rows, the plate, the
    // trim, the ruler, the flags, the stems, the scanner, the anchor) and
    // precede the modal and the flag editor's box, which float over it by
    // design — the modal's box under a value sub-page cut
    // at the keyboard's top line (paint_modal_dialog), the flag editor's box
    // in the marker lane, the cards' room ending above it
    // (notification_stack_bound). It is OUTSIDE the loading /
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
    // question's page, the progress page, a line editor's value sub-page
    // and the score viewer's box over the
    // canvas column, and the controls row — an overlay's, or a page's
    // answers — after the keyboard slot, above which a
    // value sub-page's visible part stands, and BELOW THE FLOATING SURFACES, so a refusal's
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
    // the window's top right down over the title bar, the top icon row and
    // what lies under them, send BB; the tooltip under a top-row key's face and above
    // every other owner's) and overlap whatever is
    // under them. They are NOT exposure-gated the way the rows are: each
    // writes the rect that stands (or a zero rect) on every run, and a run that
    // skipped would strand a stale rect for the hit tests and the damage to
    // read. Hidden, each costs one boolean. THE NOTIFICATION CARDS (2026-08-29)
    // publish the rect their layout gives on every run, which is what stands
    // (drawn by a run whose clip meets the card, standing from the run that
    // last drew it otherwise; paint_notifications' head), and paint BELOW
    // the tooltip and ABOVE the modal (2026-10-14, the modal's block above):
    // a hint the pointer just raised belongs on top of a message, and a
    // message on top of the box it answers. (The Settings dropdown painted between the cards
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

    // THE FLOATERS' HULLS WIDENED BY WHAT THIS RUN'S FLOATERS PUBLISHED
    // (widen_floater_hulls): every pixel a family may have standing now lies
    // in the list's hull its later rects' clears and the present read.
    widen_floater_hulls(gui_canvas_frame().floaters);

    cairo_restore(cr);

    // The target here IS memory something outside cairo will read: the
    // backend's persistent backbuffer, which the present uploads into the GL
    // texture. Flushing is cairo's handshake before anything outside cairo
    // reads that memory — it lands whatever the backend is still holding in
    // internal state, so the backing bytes are complete (paint_one_frame
    // flushes the surface once more itself before its present).
    cairo_surface_flush(cairo_get_target(cr));
}

// -- THE GL CANVAS'S FRAME (2026-10-13; canvas_sink.h owns the rule) -----
//
// The frame's draw list, built by on_redraw's first damage rect that meets
// the canvas's paint area: the canvas passes in on_redraw's paint order
// (its authoritative block), each through the sink, WHOLE — no damage
// rect cuts it and no exposure gates a pass — so the canvas the GPU draws
// whole every frame (GuiGlCanvas::present_frame's THE WHOLE DRAW) is drawn
// from one coherent state. The pixels a record changes owe damage of their own
// (canvas_sink.h's head, the damage contract). THE SWEEP HIGHLIGHT IS THE
// PLATE BRANCH'S LAST RECORD (architect 2026-11-12, paint_sweep_highlight:
// over everything in the waveform area); a frame with no audio has no
// sweep (begin_touch_region refuses unloaded audio), so the other branch
// has none.
void GuiPaintHandler::build_canvas_list(GuiCanvasDrawList& frame,
                                        bool plate_branch) {
    GuiCanvasSink sink(frame);
    const GuiRect area = waveform_area(app);
    render_canvas(sink, area);
    if (!plate_branch) {
        render_center_lines(sink, area);
        render_waveform_divider(sink, area);
        // This list holds no ring (the note's rule, FlagCache's
        // `ring_declared`).
        note_ring_drawn(GuiPhaseResetRingSides{});
        return;
    }
    paint_waveform_plate(sink, area);
    render_center_lines(sink, area);
    render_waveform_divider(sink, area);
    paint_phase_reset_overlay_ring(sink, area);
    paint_playheads(sink, area);
    paint_marker_stems(sink, area);
    paint_scanner(sink, area);
    paint_strip_drag_anchor(sink, area);
    paint_sweep_highlight(sink, area);
}

void GuiPaintHandler::begin_canvas_frame() {
    canvas_frame_seen = false;
    canvas_list_built = false;
    ++frames_begun;
}

// -- GuiPaintHandler::on_resize ------------------------------------------

void GuiPaintHandler::on_resize(int w, int h) {
    app.width  = w;
    app.height = h;
    // THE CANVAS'S KNOWN-CLEAR RECORD IS FORGOTTEN (2026-10-14; the rule at
    // on_redraw's CLEAR): the backbuffer may be new, or the client area moved
    // inside it, so the next frame clears.
    canvas_clear_known = false;
    canvas_clear_open  = false;
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
    // A re-validation writing no start, so it keeps the camera's lattice
    // (ViewportLattice::Keep, architect 2026-10-31; the rule at
    // AppState::viewport_lattice_origin).
    const double old_zoom = app.zoom_level;
    clamp_viewport_start(app, audio, ViewportLattice::Keep);
    if (app.zoom_level != old_zoom && playback.is_playing())
        playback.resync_predictor();
}
