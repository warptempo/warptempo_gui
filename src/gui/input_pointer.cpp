#include "input_handler.h"

#include "gui_display_context.h"
#include "warp_frame_map_view.h"
#include "marker_drag.h"
#include "paint_handler.h"
#include "render.h"
#include "text_editor.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <vector>

// GuiInputHandler pointer-gesture handlers (on_button_press,
// on_button_release, on_motion) and the editor-text drag finalizer
// (finalize_editor_text_drag), lifted
// verbatim from input_handler.cpp. The methods are declared on
// GuiInputHandler in input_handler.h, and the platform layer's pointer
// callbacks dispatch to them unchanged, so this is a pure definition move.
//
// The file-local ActiveEditorText / active_editor_text /
// set_editor_caret_from_x helpers live here because the press / motion /
// release editor-text drag-select is the only consumer; press, motion, and
// release all resolve active_editor_text so they agree on the editor's text
// origin and which strip to repaint.
//
// apply_editor_clipboard is intentionally NOT here — it is a keyboard
// clipboard helper used only by on_key, and stays in input_handler.cpp.

// F2.1: mouse drag-to-select for the three text editors. The selection
// highlight is already painted from the editor State's selection_anchor /
// cursor_pos, so the whole gesture is input-side: a press sets the anchor
// and arms the drag, motion moves cursor_pos (extending the highlight),
// release finalizes.
//
// ONE CLICK-TO-BYTE MAPPING (row 7, 2026-08-01). Every editor in the product is
// PROPORTIONAL now, so there is no advance to divide by anywhere: each takes an
// origin plus the shaped run's per-byte boundaries from ITS OWN painter's
// publication — the flag editor's FlagEditorBox, the three bottom-strip
// editors' BottomEditorText — and click-to-byte is the same nearest-boundary
// search over both. The monospace arm (a char-0 origin times one cell advance)
// died with the face; ActiveEditorText carries the one pair.
namespace {

// monotonic_ms() (the press-driven CLOCK_MONOTONIC ms time base for double-click
// detection) is now the shared reader declared in app_state.h — one owner, no
// per-TU clock copy.

// THE PRESS CLAIM'S HALF OF THE BUTTON ROSTER (the roster itself is
// RedesignButton, app_state.h): the KEYBOARD CHORD each row 2 button fires. The
// painter's label/icon table (paint_handler.cpp) is the other half; both key off
// the same ids. ONE roster entry is absent, and only one: row 1's SETTINGS,
// whose action is a POPUP TOGGLE — not a chord at all, since no keyboard chord
// opens or closes a dropdown. It is spelled at its own claim.
//
// The `shift` column is each button's OWN chord (only Redo's is set). It is not
// the whole shift story: the two SHIFT-ADMITTING buttons OR a shift-exact press
// into this field to reach their twins, and which buttons those are lives at
// redesign_button_shift_admits (app_state.h), which the tooltip's own table is
    // static_asserted against.
struct ToolbarChord {
    RedesignButton id;
    GuiKey         key;
    bool           ctrl;
    bool           shift;
    bool           alt;
    // (WHICH BUTTONS ADMIT SHIFT is NOT a column here: it is
    // redesign_button_shift_admits in app_state.h, because the TOOLTIP's SHIFT
    // LINE must appear exactly where a shift press does something — a
    // static_assert beside that table enforces it. One fact, two readers.)
    //
    // RADIO: this button reports a state it can only ever turn ON, so a press
    // while it is already selected is a CONSUMED NOTHING (there is nothing to
    // switch to, and its chord is a TOGGLE that would switch away from what the
    // user just clicked). The tab pair and the two view pairs are radios; the
    // follow and iteration buttons are TOGGLES and press through in both
    // directions, which is why this is a flag and not `selected` alone.
    //
    // THE VIEW BAR'S THREE ARE RADIOS FOR A DIFFERENT REASON, worth stating
    // because the toggle argument does not transfer: their chords are the
    // ABSOLUTE selectors 1/2/3, which are IDEMPOTENT — on_key's own handler
    // already makes a press on the current combination a no-op, so dispatching
    // would be harmless rather than wrong. The flag is set anyway, and for the
    // FACE: the crops give a selected face and a click face and nothing that is
    // both, so consuming at the claim keeps the pressed interior from ever
    // painting over a lit button. One rule, two justifications.
    bool           radio;
    // CLICK_FACE: rows 2 and 4 and the view bar show a pressed interior; row 1's
    // two left-floating buttons and row 3 have two faces by scope and show
    // nothing new on a press.
    bool           click_face;
};

// THE PRESS CLAIM'S HALF OF THE BUTTON ROSTER — every CHORD-DISPATCHING button
// in the redesign, rows 1 through 4, in one table. The two flags above are the
// only axes the rows differ on, so they share one dispatch body
// (dispatch_redesign_chord) instead of accumulating a special case per row.
//
// ROW 1'S TWO MENU BUTTONS ARE THE ABSENTEES, and the membership changed hands
// twice: Quit joined the table when Ctrl+Q was recognised as its chord, Settings
// left it when its action became a DROPDOWN TOGGLE (a popup open/close is not a
// chord at all — the bare `;` keyboard route still opens the editor directly,
// untouched), and Navigation arrived a menu button 2026-08-02. Everything else
// on rows 1 through 4 is here.
constexpr ToolbarChord kToolbarChords[] = {
    // Row 1. QUIT IS A CHORD — Ctrl+Q — and joined the table when the architect
    // corrected the "two-call sequence" framing (2026-07-31): on_key's own
    // Ctrl+Q route performs EXACTLY finalize_active_drags() then
    // prompt.request_close() when a gesture is live (the drag-modal gate's one
    // hatch), and prompt.request_close() alone otherwise — which is the same
    // thing, finalize_active_drags being a no-op with nothing active. So the
    // dispatch is behaviourally identical to the hand-spelled pair in every
    // state, and the pair is gone.
    {RedesignButton::Quit,       GuiKeys::Q,   true,  false, false, false, true},   // Ctrl+Q
    // Row 1's RIGHT FLOAT — the view bar (2026-08-02). Bare 1/2/3, the ABSOLUTE
    // view selectors: S+W, T+P, T+W. Everything the digits own arrives by
    // construction through on_key's own handler — the audio-first-then-markers
    // order, the refused-target-entry abort of the whole press, the coincidence
    // auto-select, the read-only admission (they are navigation), the modal
    // swallow. There is no second route to keep in step.
    {RedesignButton::ViewSW,     GuiKeys::Digit1, false, false, false, true, true}, // bare 1
    {RedesignButton::ViewTP,     GuiKeys::Digit2, false, false, false, true, true}, // bare 2
    {RedesignButton::ViewTW,     GuiKeys::Digit3, false, false, false, true, true}, // bare 3
    // Row 2 — the toolbar.
    {RedesignButton::Save,       GuiKeys::S,   true,  false, false, false, true},   // Ctrl+S
    {RedesignButton::Undo,       GuiKeys::Z,   true,  false, false, false, true},   // Ctrl+Z
    {RedesignButton::Redo,       GuiKeys::Z,   true,  true,  false, false, true},   // Ctrl+Shift+Z
    {RedesignButton::Render,     GuiKeys::R,   true,  false, true,  false, true},   // Ctrl+Alt+R (+Shift)
    // Row 3 — the tabs. Both halves carry the SAME chord: with two tabs the
    // toggle IS the direct select, and the radio flag is what makes a press on
    // the already-selected half a consumed nothing rather than a switch away.
    {RedesignButton::TabA,       GuiKeys::Tab, true,  false, false, true,  false},  // Ctrl+Tab
    {RedesignButton::TabB,       GuiKeys::Tab, true,  false, false, true,  false},  // Ctrl+Tab
    // Row 4 — the icon row. The four view buttons are radios on the same two
    // toggling chords the tabs' pair models; the rest are plain dispatches.
    {RedesignButton::IconS,      GuiKeys::T,   false, false, false, true,  true},   // bare t
    {RedesignButton::IconT,      GuiKeys::T,   false, false, false, true,  true},   // bare t
    {RedesignButton::IconW,      GuiKeys::P,   false, false, false, true,  true},   // bare p
    {RedesignButton::IconP,      GuiKeys::P,   false, false, false, true,  true},   // bare p
    // (THE ZOOM PAIR — bare `-` and bare `=` — sat here from 2026-08-01 until
    // 2026-08-02, when the architect ruled out duplicate commands on the GUI and
    // the Navigation dropdown became those two commands' one pointer home. The
    // keys are untouched; the buttons are deleted whole.)
    {RedesignButton::IconCopy,   GuiKeys::P,   true,  false, false, false, true},   // Ctrl+P
    {RedesignButton::IconPaste,  GuiKeys::P,   true,  false, true,  false, true},   // Ctrl+Alt+P (+Shift)
    // BPM'S KEY IS BARE `m`, NOT `b` — the brief expected `b` and the code says
    // otherwise (the arm is at handle_mode_keys, input_key_dispatch.cpp). The
    // button is its chord, so it takes the chord the keyboard actually has.
    {RedesignButton::IconBpm,    GuiKeys::M,   false, false, false, false, true},   // bare m
    {RedesignButton::IconIter,   GuiKeys::I,   false, false, false, false, true},   // bare i
    {RedesignButton::IconFollow, GuiKeys::F,   false, false, false, false, true},   // bare f
    {RedesignButton::IconListen, GuiKeys::L,   false, false, false, false, true},   // bare l
    {RedesignButton::IconCommit, GuiKeys::Apostrophe, false, false, false, false, true}, // bare '
};

// Is (x, y) inside the PAINTED rect of a redesigned button? The rect is the
// painter's stash and nothing here re-shapes or re-measures, so the clickable
// region is exactly the drawn one. A zero rect (before that row's first paint)
// contains no point, which is the correct cold answer.
bool redesign_button_hit(const AppState& app, RedesignButton id, int x, int y) {
    return rect_contains(
        app.redesign_buttons[redesign_button_index(id)].rect, x, y);
}

// THE WAVEFORM'S HALF SPLIT, and the ONE expression of it. The plain left press
// splits by half — upper = the placement press, lower = the audition scrub — and
// since 2026-08-03 the pointer CURSOR marks that lower half too (the Scrub cue,
// pointer_cursor_kind), so the boundary has two readers and must have one owner:
// a painted-nothing boundary and a cursor boundary that could drift by a pixel
// would be worse than either alone.
//
// It is the same arithmetic the retired 1px channel-split line was drawn on
// (that line went 2026-08-03; the split it marked did not) — integer division,
// so an odd-height area gives the lower half the extra row, which is what the
// press has always done.
bool waveform_lower_half(const GuiRect& area, int y) {
    return y >= area.y + area.h / 2;
}

// Active-domain playhead frame at click column `col`. SOURCE view: the exact
// source grid (source_grid_position_at_column via painter q), matching marker
// commits so a drop-at-playhead lands where a drag/nudge would. TARGET view:
// the domain spp form — the source-frame commit routes through the inverse map,
// so there is no source-grid claim there.
int64_t playhead_frame_at_click_column(const AppState& app,
                                       const GuiAudio& audio, int col) {
    const GuiDisplayContext& ctx = active_display_context(app, audio);
    if (ctx.domain == GuiDisplayDomain::Source) {
        const double q = painter_samples_per_pixel(app, audio, waveform_area(app));
        if (q > 0.0)
            return static_cast<int64_t>(std::nearbyint(
                source_grid_position_at_column(app.viewport_start_sample, col, q)));
    }
    const double spp = current_samples_per_pixel(app, audio);
    return app.viewport_start_sample +
        static_cast<int64_t>(std::nearbyint(static_cast<double>(col) * spp));
}

// The active editor's resolved text geometry, valid only while exactly one
// editor is active (and, for the flag editor, on-view). Press / motion /
// release all resolve this so they agree on origin and which strip to
// repaint.
struct ActiveEditorText {
    bool                valid        = false;
    text_editor::State* ed           = nullptr;  // the active editor
    double              text_left    = 0.0;       // byte-0 origin (px)
    // The painter's per-byte pen offsets for that editor's own shaped run.
    // Never null on a valid resolution — every editor is shaped since row 7.
    const std::vector<double>* byte_x = nullptr;
    bool                bottom_strip = false;      // which strip to repaint
};

ActiveEditorText active_editor_text(AppState& app, const GuiAudio& audio) {
    (void)audio;
    ActiveEditorText g;
    // THE THREE BOTTOM-STRIP EDITORS share ONE publication — only one of them is
    // ever open, and paint_bottom_strip fills it from whichever branch actually
    // painted. An invalid publication (nothing painted yet, or an editor the
    // row's precedence chain is hiding) leaves this invalid, exactly as the flag
    // editor's does: what is not on screen takes no clicks.
    const AppState::BottomEditorText& be = app.bottom_editor_text;
    const bool bottom_open =
        text_editor::is_active(app.settings_editor) ||
        text_editor::is_active(app.commit_editor) ||
        (text_editor::is_active(app.top_flag_editor) &&
         app.top_flag_editor.kind == text_editor::Kind::BpmBracket);
    if (bottom_open) {
        if (!be.valid) return g;
        g.ed = text_editor::is_active(app.settings_editor) ? &app.settings_editor
             : text_editor::is_active(app.commit_editor)   ? &app.commit_editor
                                                           : &app.top_flag_editor;
        g.text_left    = be.text_origin_x;
        g.byte_x       = &be.byte_x;
        g.bottom_strip = true;
        g.valid        = true;
        return g;
    } else if (text_editor::is_active(app.top_flag_editor)) {
        // FlagPayload — the UNROLLED FLAG BOX. Its geometry is the painter's,
        // published at app.flag_editor_box (contract at FlagEditorBox,
        // render.h): the origin already carries the view offset and the
        // boundaries are the shaped run's own pen, so there is nothing to
        // re-derive and nothing that could disagree with the pixels. An invalid
        // publication (no box painted yet, or a target the store shrank past)
        // simply leaves this invalid — the same answer the old -1 origin gave.
        const FlagEditorBox& fb = app.flag_editor_box;
        if (!fb.valid) return g;
        g.ed        = &app.top_flag_editor;
        g.text_left = fb.text_origin_x;
        g.byte_x    = &fb.byte_x;
        g.valid     = true;
        return g;
    }
    return g;
}

// The ONE click-x -> byte owner (see ActiveEditorText): a nearest-boundary
// search over the painter's published per-byte pen offsets. Every caret and
// drag-select site funnels through it, and since row 7 there is one family of
// editors rather than two, so there is nothing left for them to drift apart on.
int editor_byte_index_at(const ActiveEditorText& g, int mouse_x) {
    return text_editor::byte_index_from_shaped_x(
        static_cast<double>(mouse_x), g.text_left, *g.byte_x);
}

void set_editor_caret_from_x(const ActiveEditorText& g, int mouse_x) {
    g.ed->cursor_pos = editor_byte_index_at(g, mouse_x);
}

// Region-drag end: dissolve a resting region whose on-screen span is under the
// arm gate. The press-becomes-drag gate (kDragMovedThresholdPx) latches once
// and never re-engages, so a hand-jitter drag that crosses the gate then
// releases near the press — or wanders back toward it — can rest a sliver
// region a pixel or two wide. That was never an intentional window: a
// sub-threshold rest reads as a click, so it dissolves exactly as a plain
// click's would, clearing the highlight (the recolored ground goes, which the
// same damage covers). The
// resting-region minimum floor SCALES with the gate (deliberate): reading
// kDragMovedThresholdPx here means the sliver floor rose to 8px when the
// architect unified the gate 2026-07-24, so the smallest region that can rest
// tracks the smallest press that becomes a drag. Called
// from both region-drag end points (release and button-lost). Only the REST is
// gated — the live mid-drag extension paints slivers freely. Under
// SELECTION-FLOWS-DOWNWARD-ONLY (architect 2026-07-23) the drag never touches
// the selection: the press's deselect/drop was the committed act, and the
// drag holds no selection to clear — so this dissolve drops the region alone.
// IT DROPS NO PLAYHEAD EITHER: the drag carried the cursor to its last column
// (architect 2026-07-30) and a sliver release leaves it there, the same
// what-stands-stands rule the gesture family holds everywhere.
void end_region_drag_min_size_check(AppState& app, const GuiAudio& audio,
                                    Viewport& viewport) {
    if (!app.region.active) return;
    const double spp = current_samples_per_pixel(app, audio);
    if (spp <= 0.0) return;   // no geometry -> leave the region as-is
    const double span_px =
        std::abs(static_cast<double>(app.region.a_frame - app.region.b_frame)) /
        spp;
    if (span_px < kDragMovedThresholdPx) {
        app.region = RegionState{};
        viewport.invalidate_waveform_area();
    }
}

} // namespace

// THE MARKER LANE OWNS THE PLAYHEAD (architect 2026-07-28) — the rule this
// helper serves, stated ONCE here; the other landing sites carry only their own
// class plus a pointer back to this comment. With a non-empty selection the bare
// horizontal arrows move the FOCUSED MARKER rather than the playhead column (the
// lane model, GuiInputHandler::playhead_in_marker_lane), so:
//   * any route that changes WHICH marker is focused while the lane is active
//     LANDS the playhead on the new focus — otherwise the arrows would move a
//     marker while playhead_cursor_sample rested somewhere else entirely, and
//     Space would play from that stale spot;
//   * any route that LEAVES the lane (empties the selection) leaves the playhead
//     exactly where it is — Home/End clear rather than land for precisely that
//     reason.
// THE CURSOR PLAYHEAD IS ALWAYS PAINTED (architect 2026-07-30), which is what
// makes the rule legible rather than merely correct: the land is VISIBLE, so a
// nudge shows the cursor riding the marker it moves. (The rule used to be argued
// from a SUPPRESSION — the cursor stopped painting under a selection and the
// focused flag's ink triangle "was" the playhead — and that argument is retired
// with the suppression itself. The behaviour it justified is unchanged.)
// A LAND IS ALWAYS ON THE FOCUS (architect 2026-07-28, closing the rule): the
// three MULTI-SELECT clicks used to land at the earliest selected while focusing
// elsewhere, and no longer do — the shift-range click lands on the clicked range
// END, and both ctrl-toggle arms land on whichever marker the toggle leaves
// focused. So there is no such thing as a focus set whose land went somewhere
// else: the "towed" category is empty, and the nudge/drag follow tows nothing.
// The landing sites are enumerated below (they are this function's callers, plus
// the image-moving gestures that re-land through move_playhead_to at their own
// commits: the two position nudges, and both arms of the Up/Down cent step in
// their target-view re-warp tails).
//
// THE LAND IS A PURE PLAYHEAD WRITE: it has no region side effect whatsoever.
// The REGION is TRIM SCRATCH (its contract is at RegionState, app_state.h) and
// every command that moves the playhead or replaces the selection clears it at
// its OWN site, unconditionally — never gated on whether the playhead actually
// moved. The one authoritative clear-site enumeration lives at
// clear_region_highlight (input_handler.h); do not restate it here or anywhere
// else. What is worth stating at the land is only this: the clear is never a land
// side effect, and the land never decides it.
//
// LANDS the playhead exactly onto marker `hit` (active column's store), with
// NO viewport move — the sole difference from Tab (which recenters) and `c`
// (which re-zooms and recenters), so the view holds perfectly still while the
// playhead seats. THE CALLER INVENTORY, THE ONE AUTHORITATIVE ENUMERATION,
// re-derived by grep 2026-07-29 (other sites state their own class and point
// here; do not copy this list, re-derive it):
//   * THE POINTER CLICKS — the plain marker click (input_pointer.cpp), the
//     shift RANGE click, and the ctrl TOGGLE click. Each lands on the FOCUS its
//     own path just set: the clicked marker, the clicked range end, the
//     toggled-in marker, or the focus repaired after a toggle-out (an empty
//     post-toggle selection lands nothing). The plain click's FOUR deferred
//     completions left this list 2026-07-29 with the group drag: every marker
//     press single-selects and lands at PRESS time now (horizontal movement is a
//     focus act — the doctrine at the head of position_nudge.h);
//   * THE KEYBOARD FOCUS-COLLAPSE COMMANDS, which collapse a 2+ selection to its
//     focus and land there — the Ctrl+N inherit toggle (warpmarkers_ops.cpp) and,
//     since 2026-07-29, BOTH POSITION NUDGES through their shared prologue
//     (position_nudge.cpp). Esc's singleton rung was in this class until
//     2026-07-29, when the whole Esc ladder was deleted: bare Esc lands nothing
//     now, because it does nothing at all outside the editors, the prompts, the
//     drag swallow and the render cancel;
//   * EVERY TEXT-EDITOR OPEN — flag_editor.cpp's enter_text_edit, the one
//     chokepoint of the three open routes;
//   * THE RESTORES, which hand the lane a focus it did not have: BOTH undo/redo
//     marker arms (undo.cpp's visual tail — the singleton lands on its touched
//     marker, the group on its focus, which IS the earliest touched member) and
//     the propagate paste's CREATED-SET arm, on the FIRST created reset
//     (phase_reset_propagate.cpp — its no-created arm lands nothing now, and the
//     `p` swap lands nothing either: both used to be cleaning up a restored
//     P-column selection, and the parked slots died 2026-07-29);
//   * THE VIEW / TAB SWITCHES, which re-express a focus into a new domain: the
//     S<->T flip (input_handler.cpp) is now the only one, landing only on a
//     NON-EMPTY selection — with no lane the cursor is the playhead in its own
//     right and keeps its own value. The flip CARRIES a 2+ selection across
//     since 2026-07-30 (its collapse died with the SPAN FORM), so the selection
//     here may be a GROUP, and the land seats the cursor on its focus exactly as
//     it does for a singleton. Ctrl+Tab left this class
//     when the parked
//     selections died: it restores its tab's stored cursor VERBATIM, hands the
//     lane nothing, and its only land is the auto-select's below;
//   * THE COINCIDENCE AUTO-SELECT (auto_select_marker_at_playhead, this file) at
//     its entry chokepoints (the inventory is at its declaration,
//     input_handler.h). A provable NO-OP by
//     construction (its selection predicate IS this function's equality test), and
//     it is in the list because the adjacency is the rule, not because it moves
//     anything;
//   * (THE MAP CHANGERS ARE GONE FROM THIS LIST, architect 2026-07-29:
//     the settings engine-commit and the settings-only 'S' undo/redo arm each
//     landed here, target view only, because the rebuilt map moved the focused
//     marker's image out from under a resting cursor — and both now CLEAR THE
//     SELECTION at their own tails instead, so there is no lane and no focus left
//     to re-land. The map-change re-land SHAPE survives in exactly one place, the
//     Up/Down cent step's target-view tail, which re-lands through move_playhead_to
//     rather than here because it wants the keep-visible scroll.)
// The two-step placement
// basis the Tab family lands with (source_frame_to_active_domain then
// clamp_playhead_to_live_domain), against the active column's store, so the
// placement is exactly coincident for a subsequent nudge/drag ride. Direct
// cursor write mirroring jump_playhead_to_focused_marker's non-recenter part —
// NOT move_playhead_to, whose keep-visible edge-align could scroll for a
// half-offscreen flag, and the ruling is NO viewport write of any kind (the
// playhead may rest at a slightly offscreen column when the clicked flag hung
// half off the edge — accepted). Read-only allowed (selection + playhead are
// navigation). Some callers stop playback first (each marker click owns that
// stop at its own site, Tab-family symmetry; the restores and the bpm-editor
// open stop too); the others — the bare-Return flag-editor open,
// the `p` swap, the Ctrl+N collapse — may land DURING playback, safe because the
// land is a direct RESTING-cursor write and a live scanner is untouched by
// cursor writes (move_playhead_to's scanner-inactive
// convention). External linkage (declared in input_handler.h) so undo.cpp and
// the ops/views TUs can reach it.
void land_playhead_on_marker(AppState& app, const GuiAudio& audio,
                             Viewport& viewport, int hit) {
    int64_t src_frame = 0;
    bool valid = true;
    if (app.active_markers_view == 'P') {
        const auto& tv = app.phaseresetmarkers.markers();
        if (hit < 0 || hit >= static_cast<int>(tv.size())) valid = false;
        else src_frame = tv[hit].time_frame;
    } else {
        const auto& mv = app.warpmarkers.markers();
        if (hit < 0 || hit >= static_cast<int>(mv.size())) valid = false;
        else src_frame = mv[hit].time_frame;
    }
    if (valid) {
        int64_t sample = source_frame_to_active_domain(app, audio, src_frame);
        sample = clamp_playhead_to_live_domain(sample, app, audio);
        // IDEMPOTENCE ONLY, carrying no semantics: a land onto the sample the
        // playhead already holds writes the same value and moves no pixel, so
        // there is nothing to damage. It decides nothing about the region — the
        // caller's own clear (when the caller is a point command) runs either
        // way, before or after this call. Compared AFTER the clamp, because the
        // clamp is what decides where the land actually seats.
        if (sample == app.playhead_cursor_sample) return;
        app.playhead_cursor_sample = sample;
        // FULL WAVEFORM-AREA DAMAGE (architect 2026-07-30, replacing the narrow
        // old/new column pair computed on the LIVE viewport): the cursor's
        // pixels are PLATE-registered, and this free helper takes no
        // GuiPaintHandler, so it widens rather than adding one — a land is a
        // discrete command and a full-area invalidate cannot ride the wrong
        // epoch. Rule and per-site shape table at playhead_pixel_x
        // (app_state.h).
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
    }
}

// COINCIDENCE AUTO-SELECT (architect 2026-07-29, the entry half of THE SELECTION
// IS NEVER PARKED). Nothing stashes a selection any more, so a route that ENTERS
// a column or a tab re-acquires one from the PLAYHEAD: scan the active column's
// store and single-select the marker the playhead is already sitting exactly on.
// The payoff is the lane model — with that marker selected, the brightened flag
// and the cursor standing on it say the same thing, which is precisely the
// state the entry's stored cursor
// describes — and the price is nothing, because a selection recovered this way is
// derived from live data instead of remembered from a store that has since moved.
//
// THE TEST IS THE LAND'S OWN FORMULA, EXACTLY: clamp_playhead_to_live_domain(
// source_frame_to_active_domain(time_frame)) == app.playhead_cursor_sample,
// reusing land_playhead_on_marker's two helpers rather than re-deriving the
// mapping (a second spelling of the same conversion would drift). It is an EXACT
// int64 compare with no tolerance: the auto-select must fire only where a land
// would leave the playhead unmoved, and the whole point of the two-step basis is
// that a landed playhead is bit-exactly a marker's image. So this reads "is the
// playhead standing on a marker", not "is it near one" — a cursor one frame away
// selects nothing, which is right, since selecting it would move nothing while
// silently arming the marker lane.
// FIRST-IN-STORE WINS on a tie: markers may coincide exactly (legal in both
// stores) and in target view distinct source frames can share one target image
// under a compressing segment, so the scan is ordered and stops at the first
// match — an arbitrary but total rule, and the stores are time-ordered, so
// "first" is the earliest-authored of the coincident group.
// THE LAND AFTERWARD IS THE ORDINARY ADJACENCY, and it is a provable no-op here:
// the predicate that selected the marker is the land's own equality test, so the
// land early-returns on `sample == app.playhead_cursor_sample` and writes nothing.
// It stays because the marker lane owns the playhead — a route that hands the lane
// a focus pays the land, and this route paying it in the degenerate case is what
// keeps the rule exceptionless.
// NO REGION WORK: every caller has already cleared any resting scratch span
// before reaching here (and a region rests only beside an EMPTY selection
// anyway, so there is nothing here to invalidate).
// Read-only allowed (selection and playhead are navigation). Bounds-safe by
// construction — the index comes from the scan itself.
void auto_select_marker_at_playhead(AppState& app, const GuiAudio& audio,
                                    Selection& selection, Viewport& viewport) {
    const auto scan = [&](const auto& markers) {
        for (size_t i = 0; i < markers.size(); ++i) {
            const int64_t sample = clamp_playhead_to_live_domain(
                source_frame_to_active_domain(app, audio, markers[i].time_frame),
                app, audio);
            if (sample == app.playhead_cursor_sample) return static_cast<int>(i);
        }
        return -1;
    };
    const int hit = (app.active_markers_view == 'P')
        ? scan(app.phaseresetmarkers.markers())
        : scan(app.warpmarkers.markers());
    if (hit < 0) return;
    selection.set_single_selection(hit);
    land_playhead_on_marker(app, audio, viewport, hit);
}

// One scrub ACT: STOP, THEN START ON THE NEXT CLICK (architect 2026-07-27,
// superseding the kill-and-revive of 2026-07-23). A click on a scrub surface
// WHILE AUDIO PLAYS is a pure STOP — it does not relaunch, so the audition
// ends where the user interrupted it. The NEXT click then lands on a stopped
// session and launches a fresh one from wherever it fell, re-capturing its
// end_sample at that launch — so a scrub after a mid-session
// trim edit auditions the NEW window instead of riding a stale capture. The
// audition then plays ONCE to that end and stops; there is no looping anywhere
// in the product.
// The old exact-same-frame skip is GONE with the relaunch it existed to
// avoid: it kept an in-place audition uninterrupted, and the playing case now
// always stops, so there is no in-place audition left to preserve — and it
// cannot migrate to the stopped case, whose scanner fields are stale by
// contract (a stopped scanner is deactivated immediately; no non-playing
// validity window exists). A refused launch (out-of-window frame; target
// update in flight) leaves playback stopped, silently — the "nothing to
// audition" family; a later click at a launchable frame launches.
void GuiInputHandler::scrub_act_at(int64_t frame) {
    if (playback.is_playing()) {
        // The pure stop, through the standing stop machinery — side-effect-
        // clean here (the scrub never moved the cursor) and the owner of the
        // scanner's visible-identity teardown. `frame` is deliberately unused
        // on this arm: a click over a live session says only "stop", never
        // where to play from.
        playback_lifecycle.stop_playback_if_playing();
        return;
    }
    // Outer is_updating gate, mirroring the two Space handlers: a NEW launch
    // while a target update is in flight would audition the stale target
    // buffer, which Space refuses — so the scrub launch refuses it too,
    // silently.
    if (!(app.active_audio_view == 'T' && target_render.is_updating()))
        playback_lifecycle.scrub_launch_at(frame);
}

// The scanner scrub press body. TWO CALLERS, both in on_button_press and both
// re-derived by grepping this function 2026-08-01: the waveform LOWER-HALF PLAIN
// LEFT press, and the BARE RIGHT press anywhere in the waveform area (architect
// 2026-08-01 — full height, so it overlaps the left entry's half and extends over
// the upper half's placement press). The marker-text lane's empty-spot scrub is
// DELETED (architect 2026-07-27; that lane touches playback in neither direction
// now). Each caller owns only its own gate — the half test, the modifier
// exactness, the in-flight-gesture guard — and everything below is shared, which
// is why the second entry copied no recipe. See the declaration for
// the full contract. ONE-SHOT per click (architect 2026-07-23, the Ableton
// model): derive the clicked column's frame and run one scrub act — the press
// arms NOTHING, a held press does nothing further, and motion over the scrub
// surface is inert (the scrub drag is removed; each click pays scrub_act_at's
// stop quiescence fence AT MOST once — a stopped session's launch pays none —
// and the per-column fence cadence class is structurally gone). The caller
// keeps playback alive across the press (no waveform press stops playback, and
// the top-strip stops belong to the top-strip claims), so the
// scrub act sees the LIVE session — load-bearing for the stop-then-start
// ruling: a caller that let the session die before the act would turn the
// interrupting click into a launch, which is precisely the behaviour the
// ruling removed.
void GuiInputHandler::scrub_press_at(int click_rel_x) {
    const GuiRect area = waveform_area(app);
    // Gutter / invalid column: no launch position exists, silent no-op.
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    const int64_t frame = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    scrub_act_at(frame);
}

// THE POINTER CURSOR'S ZONE MAP. The full contract — the two callers, the zone
// table with the press branch each is taken from, what it is deliberately blind
// to, the hover-only rule and its one named exception (the live trim gesture),
// and the accepted staleness — is at the declaration in input_handler.h.
//
// The refusals below are the press's OWN gates, in the press's order, each one
// re-read out of on_button_press rather than remembered, and each applying to
// EVERY kind (this is what makes the cues hover-only — with the trim gesture's
// one named exception, stated at its arm):
//   1. the prompt swallow (the first line of the handler);
//   2. the three BOTTOM-STRIP modal editors, which return without acting — the
//      shared predicate is modal_bottom_strip_editor_active, whose second caller
//      this is;
//   3. the open dropdown, which owns the pointer and consumes every press over
//      the pixels it floats above;
//   4. the loading / empty-audio return, above the whole waveform band. The four
//      REDESIGNED ROWS are claimed ABOVE it and stay live through a load — they
//      carry no cue of their own, so they need no arm here and take the Arrow
//      from the tail like every other unnamed surface;
//   5. any live pointer gesture — the press's own `drag`/`trim_drag` guards
//      widened to any_pointer_gesture_active, the one authoritative "some
//      pointer gesture is in flight" predicate. A gesture is not a swallow but
//      it is a lie: mid-drag the button is already down and no new press can
//      start anything. ONE gesture is excepted, ahead of the refusal: a live
//      trim gesture owns the cursor (the arm below, architect 2026-08-03).
GuiCursorKind GuiInputHandler::pointer_cursor_kind(int x, int y,
                                                   GuiInputState mods) const {
    if (app.prompt.active) return GuiCursorKind::Arrow;
    if (modal_bottom_strip_editor_active()) return GuiCursorKind::Arrow;
    if (app.dropdown.open()) return GuiCursorKind::Arrow;
    if (app.loading || audio.total_frames() <= 0) return GuiCursorKind::Arrow;

    // A LIVE TRIM GESTURE OWNS THE CURSOR (architect 2026-08-03) — the ONE
    // exception to the uniform live-gesture refusal below, and the reason it
    // can be one: on this gesture alone the thing being dragged is the thing
    // the cursor names, so the cue stays TRUE for the whole drag. The kind is
    // read from the drag's own record of what it grabbed — the bridge (both)
    // keeps the bar's TrimResize, a single-bound drag keeps its own bound's
    // edge shape — never re-derived from the pointer's position: dragging a
    // bound is exactly the act of taking the pointer off the band, and the cue
    // must not flicker through the band map's answers on the way.
    // THE PENDING ARM IS THE SAME ARM, not a second one deciding differently:
    // sub-threshold the pointer still rests on the geometry it pressed (the
    // endcap, the bridge, or the ctrl click's set bound), so the pending's
    // record and the hover map name the same cue — reading the record here
    // just keeps one owner across the whole press-to-release span. (The modal
    // gates above are structurally inert mid-drag — no press or key opens a
    // prompt, editor or dropdown while the button is held — so their rank
    // costs nothing.)
    if (app.trim_drag.active || app.pending_trim_drag.active) {
        const bool both     = app.trim_drag.active
                                  ? app.trim_drag.both
                                  : app.pending_trim_drag.both;
        const bool is_begin = app.trim_drag.active
                                  ? app.trim_drag.is_begin
                                  : app.pending_trim_drag.is_begin;
        if (both) return GuiCursorKind::TrimResize;
        return is_begin ? GuiCursorKind::TrimBoundBegin
                        : GuiCursorKind::TrimBoundEnd;
    }
    if (any_pointer_gesture_active(app)) return GuiCursorKind::Arrow;

    // The waveform BAND, spelled exactly as the press spells it: full window
    // width (top.w), not the effective width, so the <=15px inert right gutter
    // counts as waveform by the user's lights on both surfaces alike. A press
    // there is a silent no-op rather than a launch, which is a degenerate
    // off-deployment case (the gutter is 0px at 1920/2560/3840) and not worth a
    // second, narrower band that would then disagree with the press.
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    const bool inside_waveform =
        x >= area.x && x < top.x + top.w &&
        y >= area.y && y < area.y + area.h;
    const bool inside_top = rect_contains(top, x, y);
    // THE TRIM BAR BAND, spelled from the ONE geometry owner the press sites read
    // (top_trim_row_area) and derived once because THREE modifier arms below need
    // it — plain (the endcap / bridge drags), ctrl and ctrl+shift (the two
    // bound-set clicks). The presses gate on the top strip first and so does this.
    const GuiRect trim_bar_row = top_trim_row_area(app);
    const bool in_trim_bar = inside_top &&
                             y >= trim_bar_row.y &&
                             y < trim_bar_row.y + trim_bar_row.h;

    // ALT-EXACT: the captured grab-pan, whose press arms on `inside_waveform`
    // alone — either half, no band split. Alt claims nothing at all in the top
    // strip (an alt press there is a strict consumed no-op), so the cue stops at
    // the waveform's edge.
    if (mods.alt && !mods.ctrl && !mods.shift)
        return inside_waveform ? GuiCursorKind::Pan : GuiCursorKind::Arrow;
    // CTRL-EXACT: two claims, and the press path's own order between them. Over
    // the TRIM BAR ctrl sets the BEGIN bound and arms a single-bound drag on it
    // (set_trim_bound_at_click_then_arm_drag) — boundary extension by another
    // route, so it takes the BEGIN cap's own cue rather than the Arrow; over the
    // waveform it is the dual-axis strip drag, `inside_waveform` alone. Ctrl's
    // other top-strip claim is the marker membership toggle, which is not a drag
    // and has no cue.
    if (mods.ctrl && !mods.alt && !mods.shift) {
        if (in_trim_bar)
            return trim_bound_click_frame(/*is_begin=*/true, x)
                       ? GuiCursorKind::TrimBoundBegin : GuiCursorKind::Arrow;
        return inside_waveform ? GuiCursorKind::Zoom : GuiCursorKind::Arrow;
    }
    // CTRL+SHIFT-EXACT: the TRIM BAR is its ONE claim in the whole product — the
    // END bound set, the begin set's mirror — so it takes the END cap's cue there
    // and the Arrow everywhere else.
    if (mods.ctrl && mods.shift && !mods.alt) {
        if (in_trim_bar)
            return trim_bound_click_frame(/*is_begin=*/false, x)
                       ? GuiCursorKind::TrimBoundEnd : GuiCursorKind::Arrow;
        return GuiCursorKind::Arrow;
    }
    // Every other combination — shift, and every mixed pair the press path
    // discards at its strict-modifier gate — is unnamed. Shift's region former
    // is the deliberate one: a real gesture with no themed cursor worth
    // borrowing.
    if (mods.ctrl || mods.alt || mods.shift) return GuiCursorKind::Arrow;

    // PLAIN-EXACT from here, and the top strip splits by band exactly as the
    // press does — ruler first, then trim bar, both disjoint from each other and
    // from the marker lane below them.
    if (inside_top) {
        // THE RULER BAND IS THE STRIP DRAG'S SECOND ENTRY (arm_strip_drag_at),
        // the same gesture the ctrl+waveform press arms — so it takes the same
        // cursor. The band is exactly top_ruler_row_area, the lane accessor the
        // press reads.
        const GuiRect ruler = top_ruler_row_area(app);
        if (y >= ruler.y && y < ruler.y + ruler.h) return GuiCursorKind::Zoom;
        // THE TRIM BAR BAND, RESOLVED THROUGH THE ROUTER'S OWN TWO OWNERS
        // (architect 2026-08-03, closing the band-wide cue this used to paint):
        // the plain press arms only on an ENDCAP or inside the inter-cap BRIDGE,
        // so the cue asks exactly hit_test_trim_endcap and
        // point_in_trim_bridge_span — the same predicates route_trim_bar_press
        // calls, in the same order — and a point on the band that arms nothing
        // (outside a trimmed-in window, either side of the bar) shows the Arrow.
        // Cue and gesture therefore agree BY CONSTRUCTION rather than by
        // proximity, which is the whole rule this map is written to.
        //
        // AN ENDCAP IS NOT THE BRIDGE: a cap moves ONE bound and the bridge moves
        // BOTH, so the caps take the boundary-extension shapes (begin left_side,
        // end right_side) and the bridge keeps ew-resize, the move.
        //
        // READ-ONLY REFUSES — the plain trim-bar press's read-only return arms no
        // drag and writes no bound (the band's sole read-only defense,
        // input_pointer.cpp), so the cue must not promise a resize a locked tab
        // will not run. The band's span-framing double-click DOES survive
        // read-only, but it is not what this cursor names.
        if (in_trim_bar) {
            if (active_view_state(app).read_only) return GuiCursorKind::Arrow;
            switch (hit_test_trim_endcap(app, audio, x, y)) {
                case TrimHit::Begin: return GuiCursorKind::TrimBoundBegin;
                case TrimHit::End:   return GuiCursorKind::TrimBoundEnd;
                case TrimHit::None:  break;
            }
            if (point_in_trim_bridge_span(app, audio, x, y))
                return GuiCursorKind::TrimResize;
            return GuiCursorKind::Arrow;
        }
        return GuiCursorKind::Arrow;
    }
    // THE WAVEFORM'S LOWER HALF: the audition scrub, through the press's own half
    // expression. The upper half is the placement press and the region former,
    // which carry no cue.
    if (inside_waveform && waveform_lower_half(area, y))
        return GuiCursorKind::Scrub;
    return GuiCursorKind::Arrow;
}

void GuiInputHandler::refresh_pointer_cursor(GuiInputState mods) {
    if (!app.pointer_in_window) return;
    gui.set_cursor_kind(
        pointer_cursor_kind(app.last_mouse_x, app.last_mouse_y, mods));
}

// Button-press handler. Verbatim from the lambda at the original
// main.cpp:1483; the captured operation-struct lambdas (begin_drag,
// drop_marker, drop_phase_reset_at_position, set_single_selection, etc.)
// are rewritten to direct method calls on the appropriate operation
// struct ref. The four hit_test_* lambdas are now free functions taking
// (app, audio, ...) explicit args. The handle_wheel lambda is now a
// private method on this struct.
void GuiInputHandler::apply_strip_drag_at(int x, int y, bool final_event) {
    // Dual-axis strip drag, INCREMENTAL (the v6 model). Every event reads the
    // LIVE zoom level and viewport and applies its own dx/dy on top — there is no
    // press baseline to go stale across composed pan/zoom phases (the earlier
    // axis-lock model died of exactly that staleness). The song anchor
    // (anchor_sample) is the zoom focus; the pan re-derives its drifted column
    // each event and the edge trick rebinds it when it leaves the screen.
    StripDragState& sd = app.strip_drag;

    // (1) Per-event deltas from the previous motion position. The crossing event
    // folds the whole accumulated delta since the press (last_x/last_y were
    // seeded there and no sub-threshold event advanced them).
    const double dx = static_cast<double>(x - sd.last_x);
    const double dy = static_cast<double>(y - sd.last_y);
    sd.last_x = x;
    sd.last_y = y;

    // (2) The old spp is read from the LIVE level (never stored).
    const double spp_old = current_samples_per_pixel(app, audio);
    const GuiRect wf_area = waveform_area(app);
    const double W = static_cast<double>(wf_area.w);
    const int64_t total = live_total_frames(app, audio);

    // (3) Pan at the old level, in the double domain: grab sign — drag right
    // (dx>0) reveals earlier content, so the viewport moves left. The result is
    // WALL-CLAMPED here, at the old level, to the SAME right wall the downstream
    // clamp_viewport_start rests at — the shared max_viewport_start_grid owner
    // (the level mid-gesture is the live level, so it reads exactly the state the
    // chokepoint would). step (5) derives the anchor column and rebinds
    // anchor_sample from vp, so both must see the viewport that will actually
    // REST. The earlier `total − W·spp_old` form sat up to a pixel short of the
    // legal grid rest — pressing at the flush-right rest first pulled vp back to
    // that off-grid wall, the anchor column clamped at W−1, and the edge rebind
    // PERMANENTLY rewrote anchor_sample, so the at-wall no-op proof failed exactly
    // at the legal rest; sharing the wall makes vp derive at the true rest and the
    // no-op proof hold. The grid snap of arbitrary INTERIOR vp values is
    // deliberately NOT reproduced (the sub-pixel residue there self-heals on the
    // following event, exactly as step (5)'s live re-read does — only the WALL had
    // to be exact because the edge rebind is a lasting mutation).
    double vp = static_cast<double>(app.viewport_start_sample) - dx * spp_old;
    const double vp_lo = 0.0;
    const double vp_hi = static_cast<double>(max_viewport_start_grid(app, audio));
    if (vp < vp_lo) vp = vp_lo;
    if (vp > vp_hi) vp = vp_hi;

    // (4) Zoom INCREMENTALLY off the live level: this event's dy applies to the
    // current level (drag DOWN, dy>0, lowers the level → zooms in). No press
    // baseline, so a wall reversal responds immediately — the older absolute-dy
    // formula had a dead zone after a clamp (dy had to unwind all the way back
    // before the level moved); this incremental form has none.
    double new_level = app.zoom_level - dy / kZoomStripPxPerLevel;
    const double max_l = effective_max_zoom_level(
        W, total, audio.sample_rate());
    if (new_level < kMinZoom) new_level = kMinZoom;
    if (new_level > max_l)    new_level = max_l;

    // (5) The anchor's drifted column under the wall-clamped post-pan viewport,
    // with the Ableton EDGE TRICK: clamp the column into [0, W-1] (the effective
    // waveform width), and when the clamp engages REBIND anchor_sample to that
    // edge pixel's frame — the zoom focus never leaves the screen; a pan that
    // pushes it to an edge PINS it there and it becomes the edge's content.
    // Deriving the column from the wall-clamped vp is what makes pure pan an
    // exact identity (see below) even at a saturated wall, and reading the live
    // viewport each event is what lets the sub-pixel grid snap self-heal on the
    // following event.
    double anchor_col = (sd.anchor_sample - vp) / spp_old;
    const double col_max = W > 0.0 ? W - 1.0 : 0.0;
    double clamped_col = anchor_col;
    if (clamped_col < 0.0)     clamped_col = 0.0;
    if (clamped_col > col_max) clamped_col = col_max;
    if (clamped_col != anchor_col) {
        sd.anchor_sample = vp + clamped_col * spp_old;
        anchor_col = clamped_col;
    }

    // Drive the capture's release-restore x to the anchor stem's surface x — the
    // identical column->x math render_strip_anchor_stem paints at (area.x +
    // col + 0.5), so on release the cursor lands dead on the stem rather than at
    // its raw traveled position (which the edge rebind leaves past a pinned
    // stem). Fired every event; the last before release wins. A motionless
    // strip press-release never reaches here, so its override stays unset and it
    // restores at the press point.
    if (set_strip_capture_restore_x)
        set_strip_capture_restore_x(
            static_cast<double>(wf_area.x) + anchor_col + 0.5);

    // (6) Apply: place anchor_sample at anchor_col under the new level's spp and
    // clamp. IDENTITY PROOFS: pure pan (dy=0) is EXACT — new_level == old, so
    // apply reproduces vp = anchor_sample - anchor_col·spp_old bit-for-bit (the
    // column was derived from that same vp), and the level-unchanged dispatch
    // takes the same synchronous full rebuild. Off the walls the pan arithmetic
    // is unchanged, so the identity holds as before; AT a wall the clamped vp
    // equals the viewport that will rest, the anchor column re-derives against it
    // consistently, and apply reproduces the wall value — a saturated pan is a
    // true no-op. Pure zoom (dx=0) leaves the anchor's current (possibly
    // edge-pinned) column fixed and pivots the rescale around it. A both-unchanged
    // event (level and viewport identical after the clamp) is a true no-op the
    // entry point skips.
    viewport.apply_strip_drag_zoom(new_level, sd.anchor_sample, anchor_col,
                                   final_event);
}

// ARM THE DUAL-AXIS STRIP DRAG at (x, y) — ONE body, TWO entries. The gesture
// had a dedicated zoom LANE, lost it when that lane was deleted (2026-07-31),
// and ROW 5 GAVE IT A SECOND ENTRY BACK: the ruler band. Both entries arm
// exactly this — the same StripDragState, the same pointer capture ("swallow"),
// the same anchor stem, the same edge clamp and 8px threshold — so the two
// surfaces cannot drift, and the ruler press is the zoom strip reborn rather
// than a lookalike.
//
// The anchor is the SONG position under the press column, which is what makes
// the zoom pivot on the pixel the user grabbed.
void GuiInputHandler::arm_strip_drag_at(int x, int y) {
    const double spp = current_samples_per_pixel(app, audio);
    app.strip_drag = StripDragState{};
    app.strip_drag.active  = true;
    app.strip_drag.press_x = x;
    app.strip_drag.press_y = y;
    app.strip_drag.last_x  = x;
    app.strip_drag.last_y  = y;
    app.strip_drag.anchor_sample =
        static_cast<double>(app.viewport_start_sample) +
        static_cast<double>(x) * spp;
    begin_strip_pointer_capture();
}

// The flag editor's guard-free close, shared by the left and right press arms
// (contract at the declaration, input_handler.h). The box is the painter's
// published rect, the same one the F2.1 caret block tests, so "outside" means
// the same thing on every press path.
void GuiInputHandler::close_top_flag_editor_for_outside_press(int x, int y) {
    if (!text_editor::is_active(app.top_flag_editor)) return;
    if (rect_contains(app.flag_editor_box.box, x, y)) return;
    flag_editor.exit_top_flag_edit_no_commit();
}

void GuiInputHandler::on_button_press(GuiMouseButton button, int x, int y,
                                      GuiInputState mods) {
    // ANY PRESS HIDES THE TOOLTIP, above every gate — it has said what it had to
    // say, and a hint left floating over the thing the user just clicked is
    // noise. It also resets the dwell, so a fresh hover starts a fresh wait.
    // Placed here rather than at the release because the hint's job ends the
    // moment the user acts on it, not when they let go.
    hide_shift_tooltip();
    // ANY PRESS ENDS THE MENU ROW'S MODE, beside it and for a related reason: the
    // ruling ends the mode on every ordinary dismissal, and with no popup open a
    // press is the only pointer act there is — the press on the anchor, the press
    // on a view-bar button, the press on the waveform underneath. THIS NEEDS NO
    // EXCEPTION LIST because the one press that must KEEP the mode re-arms
    // immediately through toggle_dropdown's open path a few lines below, which is
    // the mode's one producer; so "any press ends it" costs exactly nothing and
    // cannot be forgotten by a press route added later.
    // It is gated inside disarm_menu_row: with a popup OPEN this is inert, and
    // the press then belongs to the popup, whose own routes decide the mode.
    disarm_menu_row();
    // Prompt-modal input handling: while the bottom-strip prompt is
    // active, all mouse events are swallowed. Responses go through
    // the keyboard.
    if (app.prompt.active) return;

    // A double-click is two CONSECUTIVE clicks: snapshot the pending candidate
    // and clear the shared field here, so ANY intervening press invalidates it.
    // The consume checks below read this snapshot; each surface then re-seeds
    // its own fresh candidate (TrimBar / EditorText at a motionless release,
    // Marker / EmptyLane at the press). One closed instrumentation point — the clear covers
    // every non-consuming press (a strip/region/trim arm, a modal swallow)
    // without a clear scattered on each path.
    const DoubleClickCandidate dc_at_press = app.double_click;
    app.double_click = DoubleClickCandidate{};

    // F2.1: mouse drag-to-select inside the active text editor. A press on
    // the active editor's text region places the caret and arms a selection
    // drag (anchor == caret until the pointer moves). Resolved before the
    // per-editor modal swallows below so the gesture reaches the settings /
    // BPM bottom-strip editors too. A press outside the active editor's
    // region falls through: the bottom-strip editors stay modal and swallow
    // it, while the top flag editor closes guard-free below and the press
    // then acts normally.
    if (button == GuiMouseButton::Left) {
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            bool in_region = false;
            if (g.bottom_strip) {
                in_region = rect_contains(bottom_strip_area(app), x, y);
            } else {
                // FlagPayload: the editable text lives IN THE UNROLLED FLAG BOX.
                // The claim is the whole published BOX, pads included, not just
                // the glyph run's extent — the box is the field, and clicking
                // its padding should place the caret at the nearest end exactly
                // as clicking a text field's margin does (the nearest-boundary
                // search gives that for free). Any press OUTSIDE the box is a
                // non-caret click, which closes the editor below and then routes
                // normally (the guard-free lifecycle).
                in_region = rect_contains(app.flag_editor_box.box, x, y);
            }
            if (in_region) {
                // Double-click: a second click within the window on this
                // editor's text selects the RUN of the clicked character class
                // (word / punctuation / whitespace) under the click — select_
                // word_at's own classifier, not just a word — arming no drag.
                // The surface tag keeps it from consuming a marker / trim-bar
                // candidate.
                const DoubleClickCandidate& dc = dc_at_press;
                if (dc.surface == DoubleClickSurface::EditorText &&
                    monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                    std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                    std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                    text_editor::select_word_at(
                        *g.ed, editor_byte_index_at(g, x));
                    if (g.bottom_strip) viewport.invalidate_timestamp_area();
                    else                viewport.invalidate_top_strip();
                    return;
                }
                set_editor_caret_from_x(g, x);
                // Collapsed anchor — extends to a real selection only if the
                // pointer then moves.
                g.ed->selection_anchor = g.ed->cursor_pos;
                app.editor_text_drag.active = true;
                if (g.bottom_strip) viewport.invalidate_timestamp_area();
                else                viewport.invalidate_top_strip();
                return;
            }
            // A bottom-strip editor stays modal: a press outside its row is
            // swallowed without arming. A flag-editor press that isn't on the
            // lane text falls through to the guard-free close below.
            if (g.bottom_strip) return;
        }
    }

    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.commit_editor)) return;
    if (text_editor::is_active(app.top_flag_editor) &&
        app.top_flag_editor.kind == text_editor::Kind::BpmBracket) {
        // The BPM editor is a bottom-strip modal owner (like the settings
        // editor). Mouse input does not interact with it beyond its own
        // click-to-cursor region; the session ends only through Esc or the
        // Enter dispatch path (`m` is just a typed character now). Swallow
        // the press so it cannot drive a region drag / marker click / or
        // tear the editor down through the top-strip flag-edit routine
        // below.
        return;
    }
    // THE OPEN DROPDOWN OWNS THE POINTER, claimed above every band because it
    // FLOATS over them: a press on one of its items runs that item, and a press
    // anywhere else closes it and is CONSUMED so nothing underneath acts. The
    // presses it does not swallow are those on the MENU BUTTONS themselves,
    // which fall through to the band claim below: on this menu's own button the
    // toggle closes it — the same gesture that opened it, closing it — and on the
    // OTHER menu's button the toggle switches, closing this one as it opens that
    // one (one popup state, so the switch is free).
    //
    // SINCE THE HOVER SWITCHES TOO (2026-08-03, on_motion), that second case is
    // now the rare one from a real pointer: crossing onto the other button has
    // already switched the menu, so the press landing there finds that menu open
    // and TOGGLES IT CLOSED — the ordinary menu-bar answer for pressing the
    // button whose menu is up. Neither route changed; they simply meet here, and
    // a press that arrives with no motion before it still takes the switch
    // through this claim.
    //
    // It sits BELOW the modal gates like every other pointer target, which is
    // half of why a popup and an editor are never open together: the popup opens
    // only from a press, and while one of the three BOTTOM-STRIP editors is up
    // every press dies at those gates. The other half is not here — the
    // pointer-transparent FLAG editor swallows nothing, so a press does reach
    // the menu buttons with an edit open, and toggle_dropdown's open path ENDS
    // that edit (the rule is stated there). Two mechanisms, one claim. (The
    // reverse direction is closed by the keyboard gate: while the popup is open,
    // `;` is swallowed, so the editor cannot open under it either.)
    if (app.dropdown.open()) {
        // OWNING THE POINTER MEANS EVERY BUTTON, not just the left one
        // (2026-08-01, with the right-click scrub below): only LEFT carries
        // claims inside the popup, so any other button is CONSUMED INERT here
        // rather than falling through to the bands underneath — the popup floats
        // over the waveform, and a right press landing on the pixels it covers
        // must not scrub the audio behind it. Cheapest correct arm: the popup
        // stays open (a non-Left press is not one of its two answers, item-arm
        // or dismiss) and nothing acts.
        if (button != GuiMouseButton::Left) return;
        const AppState::Dropdown& pop = app.dropdown;
        const bool on_menu_button =
            redesign_button_hit(app, RedesignButton::Settings, x, y) ||
            redesign_button_hit(app, RedesignButton::Navigation, x, y);
        if (!on_menu_button) {
            const int hit = dropdown_item_at(x, y);
            // A MODIFIED press inside the popup closes it and does nothing else:
            // no item carries a modified binding, and leaving the popup open
            // under a press it refused would be the worse answer.
            const bool plain = !mods.ctrl && !mods.shift && !mods.alt;
            if (hit >= 0 && plain) {
                // ITEMS ACT ON RELEASE — this press only ARMS one. The whole
                // redesign fires its buttons on press; a MENU is the exception,
                // because that is the universal convention (press, slide, release
                // on what you meant) and because it is what makes the pressed
                // face worth painting at all: a press-to-act item would show its
                // accent fill for a single frame. The release body below decides
                // whether the arm becomes an action.
                if (pop.pressed_item != hit) {
                    app.dropdown.pressed_item = hit;
                    viewport.invalidate_top_strip();
                    viewport.invalidate_rect(pop.rect);
                }
                return;
            }
            // Anywhere else inside the popup, or a modified press: close and
            // consume, so nothing underneath acts.
            close_dropdown();
            return;
        }
    }

    // THE FOUR REDESIGNED ROWS (top lanes 0..3), claimed ABOVE the
    // loading/empty guard below so their buttons stay live while a file loads
    // and on a blank state — they are the surfaces that have nothing to do with
    // the loaded audio. They sit BELOW the modal gates on purpose: a press while
    // a prompt or a bottom-strip editor is up is swallowed there, exactly as it
    // is for every other pointer target (a modal owns the pointer; these buttons
    // are no exception, and every one of their chords reaches the same route
    // from the keyboard anyway).
    //
    // ONE BAND-CLAIM SHAPE FOR ALL FOUR ROWS: the exact half-open row band, a
    // press carrying CTRL or ALT is a strict consumed no-op, a SHIFT press binds
    // only where the chord table admits one, and any press in the band that is
    // not on a button is a consumed nothing. Each band differs ONLY in its rect
    // and (row 1) in the dropdown toggle of its TWO non-chord buttons, Settings
    // and Navigation, so the dispatch is ONE body, dispatch_redesign_chord,
    // driven by the table's per-button flags.
    //
    // A BUTTON's rect is the painter's stash (app.redesign_buttons, published by
    // paint_menu_row / paint_toolbar_row / paint_tab_row / paint_icon_row) —
    // never re-shaped here, so the clickable rect is the painted one. The action
    // FIRES ON PRESS: nothing on these rows drags, so there is no arm, no
    // threshold and no release body, and no double-click surface either. The one
    // thing a RELEASE does carry is the click face, cleared in
    // clear_redesign_button_press. Nothing here reads keyboard state, so the
    // bare-`e` mouse key reaches them as an ordinary left press through the
    // platform translation.
    {
        const GuiRect menu_row = top_menu_row_area(app);
        if (rect_contains(menu_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) {
                // SETTINGS AND NAVIGATION ARE THE ROSTER'S TWO NON-CHORD
                // BUTTONS, so they are spelled here rather than in the table:
                // each action is a POPUP TOGGLE, which no keyboard chord
                // performs. Their menus lead to routes the keyboard already has
                // — the bare `;` still opens the settings editor DIRECTLY, and
                // every navigation item is a key you can press instead — so a
                // dropdown is a pointer affordance for an existing road, never a
                // second one. Shift-exact is refused like every other
                // non-admitting button.
                if (!mods.shift &&
                    redesign_button_hit(app, RedesignButton::Settings, x, y)) {
                    toggle_dropdown(DropdownMenu::Settings);
                } else if (!mods.shift &&
                           redesign_button_hit(app, RedesignButton::Navigation,
                                               x, y)) {
                    toggle_dropdown(DropdownMenu::Navigation);
                } else {
                    dispatch_redesign_chord(x, y, mods);
                }
            }
            return;
        }
    }
    {
        const GuiRect toolbar = top_toolbar_row_area(app);
        if (rect_contains(toolbar, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) dispatch_redesign_chord(x, y, mods);
            return;
        }
    }
    {
        const GuiRect tab_row = top_tab_row_area(app);
        if (rect_contains(tab_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) {
                // THE ACTIVE TAB'S PADLOCK IS A SECOND TARGET INSIDE ITS TAB,
                // and the row's only one — spelled here rather than in the chord
                // table for the reason Settings is: its action is NOT A CHORD
                // the table can carry. It is BARE `o`, which toggles
                // active_view_state(app).read_only and nothing else, so the
                // padlock is that key's pointer affordance on the tab whose
                // state it names. Dispatched through on_key like every other
                // redesign button, so every keyboard gate applies identically
                // and no second writer of read_only exists.
                //
                // The rect is published ONLY for the active tab (contract at
                // AppState::tab_lock_rect), and since the slot became permanent
                // (2026-08-01) it is non-zero on every painted frame — so this
                // test is "the user clicked the active tab's lock", and the
                // dispatch TOGGLES: it locks a writable tab and unlocks a
                // read-only one, which is exactly what bare `o` does. Everything
                // else in the row — the inactive tab's whole box, its lock
                // included — falls to the chord table's Ctrl+Tab.
                // Shift-exact is refused like every other non-admitting button.
                const GuiRect& lk = app.tab_lock_rect;
                if (!mods.shift && lk.w > 0 && lk.h > 0 &&
                    rect_contains(lk, x, y)) {
                    GuiInputState chord{};
                    on_key(GuiKeys::O, chord);
                } else {
                    dispatch_redesign_chord(x, y, mods);
                }
            }
            return;
        }
    }
    {
        const GuiRect icon_row = top_icon_row_area(app);
        if (rect_contains(icon_row, x, y)) {
            if (mods.ctrl || mods.alt) return;               // strict no-op
            if (button == GuiMouseButton::Left) dispatch_redesign_chord(x, y, mods);
            return;
        }
    }

    if (app.loading || audio.total_frames() <= 0) return;
    const GuiRect area = waveform_area(app);
    const GuiRect top  = top_strip_area(app);
    // The waveform BAND spans the full window width (top.w), not the effective
    // width (area.w): the <=15 px inert right gutter counts as a waveform click
    // by the user's lights, so a plain press there still reaches the waveform
    // branch — the upper half clears the selection (it has no column to seat a
    // playhead, so that is all it does), the lower (scrub) half returns
    // silently (no launch position exists, and a scrub press touches no
    // selection anyway). The gutter is 0 px at the deployment widths
    // (1920/2560/3840 are multiples of 16), so this only matters off-deployment.
    const bool inside_waveform =
        x >= area.x && x < top.x + top.w &&
        y >= area.y && y < area.y + area.h;
    const bool inside_top = rect_contains(top, x, y);
    const bool ctrl  = mods.ctrl;
    const bool shift = mods.shift;
    const bool alt   = mods.alt;

    // Defensive: a second press during a drag is ignored (left button
    // should still be held down for a drag to exist).
    if (app.drag.active) return;
    if (app.trim_drag.active) return;

    // RIGHT-CLICK ANYWHERE ON THE WAVEFORM IS A SCRUB (architect 2026-08-01) —
    // the right button's FIRST and ONLY binding in the product. The lower-half
    // LEFT scrub is untouched; this adds the same one act over the waveform's
    // FULL HEIGHT, so the upper half's placement press and its region drag keep
    // the left button while the right one auditions without disturbing them.
    //
    // ONE OWNER: it calls scrub_press_at, the derive-column-and-act body the
    // left press already calls — the gutter no-op, the clamped column, the one
    // scrub_act_at, and "arms nothing" all come from there, so the two entries
    // cannot drift.
    //
    // BARE-EXACT (strict modifier validation): ctrl, shift and alt carry real
    // waveform bindings for the LEFT button (the strip drag, the region former,
    // the grab-pan) and none of them has a right-button meaning, so any modified
    // right press stays the standing no-op.
    //
    // NO GESTURE MAY BE IN FLIGHT. The two guards above cover only the marker
    // and trim drags; the strip drag, the grab-pan, the region drag, the editor
    // text drag and the two pendings are all reachable with the left button held
    // while a right button goes down — and a captured strip drag keeps
    // delivering button events under the pointer lock. So this gate is
    // any_pointer_gesture_active (app_state.h), the one authoritative "some
    // pointer gesture is in flight" predicate, rather than a second hand-written
    // list. The scrub itself is not a gesture and never appears in it.
    //
    // GATE PROFILE — IDENTICAL TO THE LEFT SCRUB'S, by position: everything that
    // stops the left press before it reaches scrub_press_at has already run
    // above (the prompt swallow, the three bottom-strip modal editors, the open
    // dropdown, the four redesigned rows' band claims, the loading / empty-audio
    // return). Read-only tabs scrub as ever (playback is navigation), the
    // selection / cursor / region / follow are untouched, and no double-click
    // candidate is seeded.
    // THE EDITOR LIFECYCLE RUNS ON BOTH BUTTONS (architect 2026-08-01, closing
    // the one deliberate divergence this arm shipped with — a right press used
    // to leave an open flag editor standing): a right press outside the box
    // tears the edit down exactly as a left press does, through the SAME owner
    // (close_top_flag_editor_for_outside_press), so there is no second recipe to
    // drift. It runs FIRST in the arm — ahead of this arm's own modifier and
    // in-flight-gesture gates and ahead of the scrub, so a modified right press
    // closes the edit exactly as a modified left press does and an audition
    // starts with the editor already gone. The two arms sit at the SAME depth
    // otherwise: both are below the modal, popup and band claims, so a right
    // press swallowed up there closes nothing, and neither does a left one. A
    // right press INSIDE the box is still nothing at all: the box is the field,
    // and the right button has no meaning on it.
    if (button == GuiMouseButton::Right) {
        close_top_flag_editor_for_outside_press(x, y);
        if (ctrl || shift || alt) return;
        if (any_pointer_gesture_active(app)) return;
        if (!inside_waveform) return;
        scrub_press_at(x - area.x);
        return;
    }

    // Mouse authoring is home-view gated like the keyboard: placement arming is
    // gated by active_column_authoring_allowed, off-home selecting and landing but
    // arming NOTHING — with no exception anywhere, since 2026-07-29. W+target used
    // to arm the TEMPO drag on an eligible marker instead of the reposition drag
    // (the pointer half of the home-view binding's tempo exception); that whole
    // gesture is deleted (see marker_drag.h), so a W+target flag press now selects
    // and lands like any other off-home press and arms nothing at all. The
    // click-playhead / region-drag family below is
    // navigation, not authoring, and stays view-independent.

    if (button == GuiMouseButton::Left) {
        // Editor lifecycle, guard-free — THE SHARED OWNER, called from the right
        // press arm above too. A press in the editor's rendered lane text
        // already repositioned the caret / armed the text drag above (the F2.1
        // block) and returned; ANY other left press with the top flag editor
        // open CLOSES it without committing, and then FALLS THROUGH so the press
        // acts normally (arm a strip drag, select a marker, arm a marker drag,
        // land, place the playhead, ...). Placed ahead of every claim below so
        // the close really is unconditional. Consequence: a double-click on the
        // open editor's own marker is close-then-reopen — the first click
        // closes + selects + seeds a Marker candidate (+ arms the pending drag
        // on the flag part, writable), and the second consumes into a fresh
        // open. That IS the documented "double-click opens the editor"; there
        // is no own-marker special case.
        close_top_flag_editor_for_outside_press(x, y);

        // The marker hit, computed ONLY on the path that consumes it. The
        // marker is ONE pointer item and that item is now its FLAG BOX alone
        // (hit_test_flag against the painter's stash — the rendered lane run
        // that used to be its second half died with the marker-text lane, and
        // with it the MarkerHit pair and its shared resolver marker_hit_at).
        // The TOP-STRIP hit feeds the plain/Shift/Ctrl
        // marker-click branches (plain = single-select + land the playhead on
        // the marker + double-click seed / consume + arm the pending marker
        // drag on the flag part, Shift = file-manager inclusive RANGE select
        // from the interaction's anchor (shift-held, else the adopted focus)
        // to the clicked marker + land on that range END, Ctrl = the individual
        // membership toggle + land on the resulting focus), so it is resolved
        // once here — every one of the three lands on its own focus.
        // The WAVEFORM never SELECTS a marker by HIT — a plain press splits by half
        // (upper: deselect-all + playhead placement + region-drag arm; lower:
        // the scanner scrub, which touches no selection at all), and a Shift
        // press FORMS a region waveform-wide (from the playhead, or a marker
        // DROP that clears the selection; see the waveform block below) — so
        // no marker scan runs on the waveform at all (the invisible stem is
        // not a grab target). The plain DRAG never selects markers either
        // (SELECTION FLOWS DOWNWARD ONLY, architect 2026-07-23 — the region no
        // longer selects its contents; it leaves the selection empty).
        // Trim bounds are grabbed only by their top-strip endcaps /
        // the inter-endcap bridge on a PLAIN trim-bar press (route_trim_bar_press
        // below); a click over a bound's waveform stem is an ordinary waveform
        // click (the stem grab retired), so no trim hit test runs on the
        // waveform at all. Resolved ONCE here, ahead of every branch that
        // consumes it: the ctrl-exact membership toggle, the plain / Shift
        // marker click, and the empty-top-strip fallthrough (mh_index < 0 is
        // what makes a spot EMPTY) all read this one hit.
        int mh_index = -1;
        if (inside_top) mh_index = hit_test_flag(app, audio, x, y);
        // THE STEM IS A SECOND SURFACE OF THE SAME ITEM (architect 2026-08-01):
        // a press within a few px of an ENABLED marker's stem column, in the
        // WAVEFORM'S UPPER HALF, resolves to that marker and routes through the
        // very same click bodies its flag does — select / land / arm / open,
        // nothing restated. It is resolved HERE, beside the flag hit, so exactly
        // one `mh_index` reaches every branch below and the two surfaces cannot
        // answer differently. The full contract (why upper-half only, why the
        // painter's stash, the tie rule) is at hit_test_marker_stem, app_state.h.
        //
        // THE STEM SURFACE IS PLAIN-EXACT (architect 2026-08-01, second pass):
        // the SHIFT range select and the CTRL membership toggle bind to the FLAG
        // ALONE, because both modifiers already mean something else ON THE
        // WAVEFORM — ctrl is the strip drag and shift is the region former — and
        // the stem lives in the waveform area, standing over pixels those two
        // gestures own. So a modified press near a stem is NOT a marker hit at
        // all: it falls through to the waveform's own ctrl / shift gesture at
        // that column, which is the answer the surface underneath promises. The
        // FLAG keeps all three clicks (it is in the top strip, where neither
        // waveform gesture reaches), and the plain stem click is untouched —
        // select, land, arm the pending drag, seed the double-click.
        //
        // ONE HIT OWNER STILL: the gate is on this single resolution site, not a
        // second hit function, and the short-circuit leaves `mh_index` at -1 for
        // a modified press, so every branch below sees "no marker" from the one
        // index they all read.
        //
        // `stem_click` is what widens the marker branches' own gate from
        // "inside_top" to "inside_top OR a stem": the trim-bar and empty-lane
        // arms inside those branches are y-band tests that a waveform press
        // fails, so they fall through untouched.
        const bool stem_click =
            !inside_top && inside_waveform && !alt && !ctrl && !shift &&
            (mh_index = hit_test_marker_stem(app, x, y)) >= 0;

        // A top-strip press stops playback WHEN IT CLAIMS SOMETHING, never
        // merely because it landed in the strip (architect 2026-07-27). The
        // stop is the price of an authoring or a navigation act — a marker
        // select, a trim bound set, a trim-bar consume — and continuing audio
        // during authoring / text editing is the wrong default, so each of
        // those acts calls stop_playback_if_playing ITSELF at its own site
        // below. THE STOP IS INTENTIONAL, NOT POSITIONAL: a press that claims
        // NOTHING changes no state at all, so there is nothing for a stop to
        // protect and a live audition survives it. A claim that can still
        // REFUSE goes one step further (architect 2026-07-27): its stop sits
        // INSIDE the refusal gate, at the latest point before the mutation, so
        // a claimed-but-refused press (a bound set in a read-only tab, over a
        // degenerate audio/geometry state, or at a column not STRICTLY INSIDE the
        // partner bound — the 2026-08-01 guard)
        // is as playback-inert as an unclaimed one. That covers every modified
        // combination the branches below reject (alt on a marker, ctrl or alt
        // on an empty flag/triangle spot, ctrl+alt, shift+alt, ...), a
        // SHIFT-exact trim-bar press (trim is transparent to shift), a
        // SHIFT-exact empty flag/triangle-lane press (shift binds nothing on
        // that lane — the waveform's shift region former does not extend to
        // it), every empty marker-text-lane spot, and the inter-lane gaps — all
        // of which end at the inert top-strip return far below.
        // The EMPTY FLAG/TRIANGLE lane's PLAIN press is the one ACTING press
        // that still does not stop: it is the waveform-upper-half's twin (the
        // empty flag/triangle-lane parity press, architect 2026-07-23), and a
        // live session RESEEKS there
        // rather than dying, through place_playhead_and_arm_region. That
        // exemption is stated at the branch itself and is PLAIN-ONLY by
        // construction — the branch matches no modified press at all, so shift
        // sits with ctrl and alt in the inert list above and stops nothing
        // there either.
        // Waveform clicks keep playback alive as ever — the per-press reseek to
        // the click sample happens at the playhead-drag press site below, gated
        // on was_playing && sample != playhead_at_entry. Capture the entry
        // state up front, AHEAD OF EVERY STOP below, so all the downstream
        // branches see the same snapshot.
        const bool was_playing = playback.is_playing();
        const int64_t playhead_at_entry = app.playhead_cursor_sample;

        // Clicks in iter/BPM mode route through the unified marker
        // hit-test below.

        // Only presses inside the waveform or the top strip do anything.
        if (!inside_waveform && !inside_top) return;

        // Alt-exact left press: on the waveform it arms the captured grab-pan;
        // alt-exact anywhere else (a top-strip marker included) does nothing
        // further HERE — the land now lives on the PLAIN marker click below.
        // On a markerless (or top-strip) spot that "nothing" is a STRICT no-op,
        // playback included: alt claims no top-strip gesture, so no stop runs
        // over it and a live audition keeps playing; alt+drag from a marker is
        // inert by ruling.
        //
        // The waveform grab-pan: continuous 1:1 pan of the viewport by the
        // per-event pixel delta (see on_motion). It CAPTURES the pointer
        // (begin_strip_pointer_capture, the same cursor-hide + lock the zoom
        // strip uses) so the pan travels infinitely under unbounded virtual
        // coordinates while the viewport clamps at the song walls — pan-only, no
        // zoom axis and no anchor stem (the stem is the zoom pivot affordance,
        // gated on strip_drag.active). Navigation-class: allowed in read-only,
        // never touches the playhead or selection. It deliberately does NOT
        // override follow — a pan during playback moves the view along with the
        // audio rather than signaling a stop, unlike the marker / trim / playhead
        // gestures. A motionless Alt press-release commits nothing but the brief
        // cursor hide/reappear (the scroll happens on motion).
        if (alt && !ctrl && !shift) {
            if (inside_waveform) {
                app.scroll_drag = ScrollDragState{};
                app.scroll_drag.active = true;
                app.scroll_drag.last_x = x;
                begin_strip_pointer_capture();
            }
            return;
        }

        // Ctrl-exact left press splits by surface. On a top-strip MARKER it is
        // the individual membership toggle + land on the resulting focus (the
        // marker claim below). On the WAVEFORM it arms the dual-axis strip drag
        // (StripDragState / apply_strip_drag_at) — ONE OF THE GESTURE'S TWO
        // ENTRIES. It was the only one between the zoom lane's deletion
        // (architect 2026-07-31) and row 5, which is what proved the deletion
        // cost no capability: this press has the gesture's full reach — the
        // cursor capture ("swallow"), the anchor stem, the edge clamp, and
        // dual-axis zoom+pan. Row 5 gave it a second entry on the RULER band
        // (arm_strip_drag_at, the zoom strip reborn), through the same hoisted
        // arm body, so the two cannot drift. The waveform strip-drag is
        // navigation-class: allowed in read-only, never touches the playhead or
        // selection — and a MOTIONLESS ctrl+waveform press-release commits
        // nothing at all (the ctrl+waveform selection clear is RETIRED,
        // architect 2026-07-23: ctrl is purely the zoom modifier on the waveform).
        // Ctrl-exact on a MARKERLESS top-strip spot is a strict no-op except
        // the trim bar's BEGIN bound set (the claim below).
        if (ctrl && !alt && !shift) {
            // Ctrl-exact on a top-strip MARKER is the individual membership
            // toggle (the former shift behavior) — this AMENDS the "Ctrl keeps
            // only the letter chords" rule for the one marker surface (architect
            // 2026-07-23). It arms no drag, seeds/consumes no double-click, opens
            // no editor. Whether the toggle ADDED or REMOVED, the playhead lands
            // on the FOCUS the toggle leaves behind (architect 2026-07-28,
            // replacing the earliest-selected land): an ADD focuses the clicked
            // marker, a REMOVE of the focused member repairs the focus to the
            // largest remaining index, and a REMOVE of any other member leaves
            // the focus alone — so app.last_selected_marker is the one expression
            // for all three, and it is always a live member here (a non-empty
            // selection always carries a focus after either arm). ANY RESTING
            // REGION CLEARS, unconditionally and whatever the land did or did
            // not move: the region is trim scratch, never a selection visual, so
            // there is no result-size split here any more (the >=2 arm's extent
            // write died with the SPAN FORM, architect
            // 2026-07-30). Read-only allowed
            // (selection + playhead are navigation).
            //
            // THE FLAG IS THE WHOLE SURFACE HERE — `inside_top` alone, not the
            // stem's widened gate (architect 2026-08-01): the ctrl-exact
            // WAVEFORM press is the strip drag, and the stem stands ON the
            // waveform, so ctrl over a stem belongs to the drag. The gate is
            // spelled `inside_top` rather than `inside_top || stem_click`
            // because stem_click is FALSE for any modified press by its own
            // definition above; saying so directly keeps this branch from
            // reading as though a stem could still reach it. A markerless
            // top-strip ctrl press claims only the trim bar (BEGIN bound set,
            // next block) and is a strict no-op on every other lane — no-op in
            // the playback sense too, since only a CLAIM stops playback.
            if (inside_top && mh_index >= 0) {
                // The toggle is an act, so it owns its stop: selecting while a
                // session plays is the authoring case the top-strip stop
                // exists for. It runs AHEAD of the toggle and the land, like
                // every other claim's stop — and it stays at the TOP of the
                // branch because this claim cannot refuse: read-only is allowed
                // (selection is navigation), the hit index is >= 0 by the gate
                // above (so the mutator's idx < 0 guard is unreachable here),
                // and every path below changes membership.
                playback_lifecycle.stop_playback_if_playing();
                selection.toggle_selection_membership(mh_index);
                if (!app.selected_markers.empty())
                    land_playhead_on_marker(app, audio, viewport,
                                            app.last_selected_marker);
                // A marker click is a POINT command, so it takes any resting
                // scratch span with it (the clear-site list is at
                // clear_region_highlight, input_handler.h).
                clear_region_highlight(app, viewport);
                return;
            }
            // Markerless top-strip ctrl-exact press: the TRIM BAR sets the BEGIN
            // trim bound at the click (REINSTATED architect 2026-08-01 — ctrl is
            // BEGIN and ctrl+shift is END, the pair's original shape, now homed
            // on the redesigned bar's whole band rather than the chip row it grew
            // up on; set_trim_bound_at_click refuses a read-only tab silently —
            // the clicks ADJUST the window that always rests, they never create
            // one — refuses any value not STRICTLY INSIDE its partner, and, being
            // a SETTER, deselects past its refusals). EVERY other lane is a
            // strict no-op, falling through to the return below (the ctrl-click
            // clear on an empty marker spot is RETIRED, architect 2026-07-23:
            // ctrl-click in Ableton is just click, and ctrl stays the zoom
            // modifier here; the PLAIN empty marker-lane press below is the
            // surviving lane gesture — the waveform's own parity press, not a
            // bare clear). The four redesigned rows (lanes 0..3) were claimed far
            // above and never reach here.
            //
            // THE BAND IS THE CURRENT GEOMETRY OWNER, top_trim_row_area — the
            // exact band the plain endcap/bar drags and the span-framing
            // double-click claim, so paint, hit and every trim gesture read ONE
            // accessor and cannot drift.
            if (inside_top) {
                const GuiRect trim_band = top_trim_row_area(app);
                if (y >= trim_band.y && y < trim_band.y + trim_band.h) {
                    // NO stop here: the bound set has its own refusals
                    // (read-only, a degenerate audio/geometry state, a value not
                    // strictly inside its partner), and a refused press changes
                    // nothing, so there is nothing for a stop to protect. The
                    // stop lives INSIDE set_trim_bound_at_click, past every
                    // refusal and immediately ahead of the bound write.
                    // Set the BEGIN bound AND arm the single-bound drag on it, so
                    // motion drags it live (a motionless release rests the set;
                    // a refused set arms nothing).
                    set_trim_bound_at_click_then_arm_drag(/*is_begin=*/true, x, y);
                    return;
                }
            }
            if (inside_waveform) arm_strip_drag_at(x, y);
            return;
        }

        // Ctrl+Shift-exact: the TRIM BAR is its ONE claim — set the END trim
        // bound at the click (ctrl is BEGIN, ctrl+shift is END; the same
        // reinstated pair, architect 2026-08-01. set_trim_bound_at_click refuses a
        // read-only tab silently — the adjust-only pair gate died with the unset
        // state 2026-07-30, a full pair always resting — refuses any value not
        // strictly inside its partner, and deselects as a SETTER past its
        // refusals). Everywhere else Ctrl+Shift stays a strict no-op, playback
        // included, falling to the return below.
        if (ctrl && shift && !alt && inside_top) {
            const GuiRect trim_band = top_trim_row_area(app);
            if (y >= trim_band.y && y < trim_band.y + trim_band.h) {
                // NO stop here either: like the BEGIN set above, the stop sits
                // inside set_trim_bound_at_click past that act's refusals, so a
                // refused END set leaves a live audition alone.
                // Set the END bound AND arm the single-bound drag on it.
                set_trim_bound_at_click_then_arm_drag(/*is_begin=*/false, x, y);
                return;
            }
        }

        // Strict modifier matching: the marker reposition arm lives on the plain
        // flag press and trim's endcap/bridge drags on the plain trim-bar press, so
        // every remaining modified combination — Ctrl+Alt (a strict no-op),
        // Ctrl+Shift off the trim bar (its one claim is the END bound set
        // above), Shift+Alt, Ctrl+Alt+Shift, ... — no-ops
        // here. Only a plain
        // or Shift-on-the-top-strip base press proceeds (Shift adjusts the
        // marker selection). Alt is POINTER-ONLY vocabulary: the Alt+wheel
        // stepped pan and the Alt+drag captured grab-pan are untouched (separate
        // handlers). On the keyboard alt survives only in the FOUR Ctrl+Alt
        // render / propagate chords (Ctrl+Alt+R, Ctrl+Alt+Shift+R,
        // Ctrl+Alt+P, Ctrl+Alt+Shift+P) — every other alt keybinding was retired
        // 2026-07-28, so nothing here defers to one.
        // Discarding a press here is TOTAL: it claimed
        // nothing, so it stopped no playback on the way down either — the stops
        // live at the claims above and below, never on the route to this gate.
        if (ctrl || alt) return;

        // Plain or Shift press. In the waveform area a plain press splits by
        // HALF: the UPPER half clears the marker selection (deselect-all),
        // places the playhead at the clicked column, and arms the
        // region-select drag; the LOWER half is the scrub surface (one scrub
        // act, nothing else) — neither ever SELECTS a marker. A Shift press on
        // the waveform instead FORMS a region waveform-wide
        // (the former / marker drop, one-shot — see the waveform block). In the top strip a plain
        // TRIM-BAR press arms a trim endcap/bridge drag (claimed ahead of the
        // marker select); otherwise a marker click — its FLAG BOX, the marker's
        // one pointer item — is the whole selection interface, BOTH views. Plain
        // click: single-select, LAND the playhead on the marker (below), and ARM
        // a pending marker drag (moves the marker if the pointer crosses the
        // threshold, else a pure click). Shift+click: a
        // file-manager INCLUSIVE RANGE select from the interaction's anchor
        // (shift-held, else the adopted focus) to the
        // clicked marker (the range end = FOCUS), which LANDS the playhead THERE.
        // The individual membership TOGGLE moved to Ctrl+click (the ctrl-exact
        // marker claim in the earlier branch; whether it adds or removes it lands
        // on the focus it leaves, an empty selection landing nothing). The
        // plain / shift / ctrl land makes every such marker click a land route
        // alongside the Tab family and `c` (which additionally recenter /
        // re-zoom), and all three land ON THEIR FOCUS (architect 2026-07-28), so
        // the playhead and the focused flag are coincident before any subsequent
        // drag or nudge — nothing is towed.
        if (inside_top || stem_click) {
            // A PLAIN STEM CLICK ENTERS HERE TOO (2026-08-01). Its y is in the
            // waveform, so every band test inside this branch — the trim bar
            // lane below, the ruler's strip-drag arm, the empty-marker-lane parity
            // press — simply fails for it, and it lands on the one arm it is
            // for: `mh_index >= 0`, the marker click. Nothing in those bodies
            // knows or needs to know which surface resolved the index.
            //
            // A SHIFT PRESS NEVER ARRIVES BY THE STEM: stem_click is plain-exact
            // (its definition above), so shift over a stem leaves both halves of
            // this gate false and falls through to the waveform block, where
            // shift is the region former. Shift reaches the marker RANGE select
            // through the FLAG BOX alone, which is `inside_top`.
            //
            // The TRIM BAR (top_trim_row_area, lane 4) is trim's lane and is
            // claimed BEFORE the marker single-select. Row 5's three lanes —
            // the trim bar, the ruler (lane 5) and the marker lane (lane 6) —
            // are disjoint y-bands, so
            // this contends with nothing: a marker-part press falls to the marker
            // handling below. The PLAIN click consumes the span-framing
            // double-click, else arms an endcap/bridge drag, else — on an unclaimed
            // spot — is a CONSUMED NOTHING; the bound-set clicks
            // are the ctrl (BEGIN) / ctrl+shift (END)
            // claims above. A SHIFT-exact trim-bar press claims nothing —
            // trim is transparent to it, and the shift fall-through below is
            // inert here (the flag boxes' y-band excludes the trim row), so it
            // ends at the inert top-strip return, having touched nothing at
            // all: it is the only press that reaches the trim bar without
            // claiming it (ctrl and alt were discarded at the gate above), and
            // it stops no playback — nor does the plain press: the trim bar's
            // stop belongs to the DRAG's first accepted bound change
            // (input_trim.cpp).
            // THE RULER BAND IS THE ZOOM STRIP REBORN (row 5): a PLAIN left
            // drag here arms the dual-axis strip drag through the shared arm
            // above — the gesture's second entry, beside the ctrl-waveform one.
            // Claimed before the trim and marker bands (disjoint y-bands, so it
            // contends with nothing) and NAVIGATION-CLASS: allowed in read-only,
            // touching neither playhead nor selection.
            //
            // A MOTIONLESS plain press-release is a CONSUMED NOTHING — the
            // release body disarms without committing, exactly as the
            // ctrl+waveform entry does. There is NO DOUBLE-CLICK SURFACE here:
            // the span-framing double-click lives on the TRIM lane, and giving
            // the ruler one too would make two neighbouring bands answer the
            // same gesture differently.
            //
            // THE BAND IS EXACTLY top_ruler_row_area AND NOTHING BELOW IT, and
            // that survived the head's move into the marker lane unchanged
            // (2026-08-01) because the claim was never keyed on the head — it
            // reads the lane accessor and only the lane accessor. So the ruler
            // lane is now labels + tick-tops + this drag, the marker lane is
            // head + flags + their routes, and a press in the marker lane can
            // never arm the strip drag: it falls past this block to the marker
            // hit and the empty-marker-lane parity press below.
            {
                const GuiRect ruler = top_ruler_row_area(app);
                if (!shift && y >= ruler.y && y < ruler.y + ruler.h) {
                    arm_strip_drag_at(x, y);
                    return;
                }
            }
            const GuiRect trim_bar = top_trim_row_area(app);
            const bool in_trim_bar =
                (y >= trim_bar.y && y < trim_bar.y + trim_bar.h);
            if (!shift && in_trim_bar) {
                // Plain trim-bar press. In a writable tab an endcap/bridge hit ARMS
                // the trim drag (a motionless release then runs that same click
                // action at on_button_release); read-only cannot arm one. Either
                // way the trim bar CONSUMES the press — it never falls to the
                // marker handling.
                // A PLAIN TRIM-BAR CLICK THAT NEVER BECOMES A DRAG IS NOW A
                // CONSUMED NOTHING (architect 2026-07-30). Its entire act was
                // PUBLISHING the trim window as a region highlight, and that
                // coupling is retired outright with the SPAN FORM — so the two
                // highlight-only arms that stood here (the read-only press's
                // direct sync and the writable UNCLAIMED-spot sync) are gone, and
                // so is the pre-route stop that served them: keeping either would
                // make a no-op click stop an audition and destroy a selection for
                // nothing. The drag keeps both — it takes the setter's deselect
                // and the trim-mutation stop at its FIRST ACCEPTED bound change
                // (input_trim.cpp), which is where a trim commit actually happens.
                //
                // THE SPAN-FRAMING DOUBLE-CLICK, rehomed onto this band from the
                // deleted zoom lane (architect 2026-07-31): the whole band —
                // endcaps, bridge and empty space alike — carries it, since it
                // frames rather than grabs. The CONSUME runs FIRST, ahead of
                // every arm: a candidate from the previous motionless press-
                // release on this band, inside kDoubleClickMs and the slack on
                // both axes, spends this press on the framing command and returns
                // — no drag armed, no bound touched, playhead and selection
                // untouched, allowed in read-only because it is pure navigation
                // (all modal gates sit far above). The surface tag is what keeps
                // a marker or editor candidate from consuming here. It DIVERGES
                // from the bare `0` key, which toggles the working zoom;
                // this frames the region, else a proper trim sub-window, else the
                // whole song.
                {
                    const DoubleClickCandidate& dc = dc_at_press;
                    if (dc.surface == DoubleClickSurface::TrimBar &&
                        monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                        std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                        std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                        run_span_framing_command();
                        return;
                    }
                }
                // SEEDING is a RELEASE act (only the release knows the press
                // stayed still), so the press records its point and the release
                // decides — see TrimBarPressSeed. Recorded ABOVE the read-only
                // return: a locked tab arms no drag but still frames.
                app.trim_bar_press = TrimBarPressSeed{
                    .active = true, .press_x = x, .press_y = y};
                // THIS RETURN IS THE SOLE READ-ONLY DEFENSE FOR THE WHOLE
                // TRIM-BAR BAND, recorded here per the routing-gate rule
                // (docs/engineering/validation_topology.md): the band is
                // consumed either way, so a locked tab arms no drag and writes
                // no bound, and route_trim_bar_press below deliberately carries
                // NO read-only check of its own — it has exactly one caller,
                // this one, so a second check there would be unreachable
                // (deleted 2026-08-02, the handle_trim_x precedent). Anything
                // that ever calls that router from a second site inherits this
                // gate's job and must state where it discharges it. The ctrl /
                // ctrl+shift bound-set press is NOT such a site: it never routes
                // through here, and set_trim_bound_at_click owns its own refusal.
                if (active_view_state(app).read_only) return;
                route_trim_bar_press(x, y);
                return;
            }
            if (mh_index >= 0) {
                // A marker click — plain or Shift — is
                // an act (select, land, arm, open), so it owns the stop for the
                // whole branch: selecting or editing under a live audition is
                // the case the top-strip stop was written for. One site ahead
                // of BOTH arms below (the range select, and the plain
                // single-select + land + editor open),
                // and it belongs at the TOP because NEITHER arm refuses:
                // read-only still selects and lands, the hit is >= 0 by the
                // gate (the mutators' idx < 0 guards are unreachable), and the
                // plain arm always single-selects and lands. Only the editor
                // OPEN can decline (read-only / P view / off home), and that is
                // a second act layered on a click that already committed.
                playback_lifecycle.stop_playback_if_playing();
                const int hit = mh_index;
                if (shift) {
                    // Shift+click is a file-manager INCLUSIVE RANGE select
                    // (architect 2026-07-23): the click ranges from the
                    // interaction's anchor — a LIVE anchor, else the ADOPTED
                    // FOCUS (architect 2026-07-23: plain-click A
                    // then shift+click B selects A..B; with nothing focused the
                    // click anchors on its own marker, selection {hit}). THE
                    // ANCHOR IS NOT KEYED TO THE PHYSICAL SHIFT HOLD (architect
                    // 2026-07-29): it SURVIVES a shift release and dies at the
                    // next membership replace, so a shift interaction re-started
                    // after a release ranges from the SURVIVING anchor, not from
                    // the focus — A..B, release, re-press, shift-click C gives
                    // A..C, the accepted delta of the falling-edge hook's
                    // deletion. The full contract and clear list live at
                    // app.shift_range_anchor's declaration (app_state.h). Each
                    // successive shift-click replaces the selection with the
                    // inclusive index range between that anchor and
                    // hit. The clicked marker becomes the range end = FOCUS
                    // (last_selected), and the playhead LANDS THERE (architect
                    // 2026-07-28, replacing the earliest-member land): focus and
                    // land no longer diverge on any click, so nothing is towed
                    // onto the focus by a later nudge. On an anchoring
                    // focus-less first click the selection is {hit} and hit is
                    // the focus, so the land is unchanged. Ctrl+click is the
                    // individual membership toggle (above, landing on its own
                    // resulting focus). It arms no drag, seeds/consumes no
                    // double-click, opens no editor. Allowed in read-only (selection + playhead are
                    // navigation). ANY RESTING REGION CLEARS whatever the
                    // result size (architect 2026-07-30): the region is trim
                    // scratch, not this selection's extent, so the >=2 arm's
                    // extent write died with the SPAN FORM and the split with
                    // it.
                    selection.select_range_from_anchor(hit);
                    // A range leaving exactly one selected shows its always-on stem;
                    // select_range_from_anchor owns the subject-change damage.
                    // The land target is the FOCUS the mutator just set, which is
                    // `hit` on both its arms — spelled as last_selected_marker so
                    // the three multi-select clicks read as one rule.
                    if (!app.selected_markers.empty())
                        land_playhead_on_marker(app, audio, viewport,
                                                app.last_selected_marker);
                    // A marker click is a POINT command and takes any resting
                    // scratch span with it.
                    clear_region_highlight(app, viewport);
                } else {
                    // ONE PLAIN MARKER PRESS, NO SPECIAL CASE FOR A SELECTED
                    // MEMBER (architect 2026-07-29, HORIZONTAL MOVEMENT IS A FOCUS
                    // ACT — the doctrine is at the head of position_nudge.h):
                    // a press on a member of a 2+ selection single-selects and
                    // lands IMMEDIATELY like a press on any other marker, and the
                    // drag it arms is an ordinary singleton drag. The two
                    // file-manager DEFERRALS that used to sit here — one per drag
                    // surface, each holding the click's committed act back so the
                    // drag could seed the intact group —
                    // died with the group drag itself; groups are never moved by
                    // any route, so nothing needs the selection to survive the
                    // press. (The second of those surfaces, the tempo drag, is
                    // gone outright — see marker_drag.h.)
                    // Plain marker click single-selects (both views; W's
                    // click-to-edit is retired — the editor now opens on Enter or
                    // this double-click). Selection is navigation, allowed in
                    // read-only.
                    selection.set_single_selection(hit);
                    // The clicked marker's flag BRIGHTENS here —
                    // set_single_selection damages the top strip, where the
                    // flags live. No stem work is
                    // owed on any arm, the click-an-already-selected no-op
                    // included: stems are class-colored and always on, so a
                    // membership change never creates, moves or recolors one.
                    // Covers the double-click-consume path
                    // below too (this single-select ran first).
                    // ...and LANDS the playhead exactly onto the marker (shared
                    // helper; see land_playhead_on_marker). Runs on EVERY plain
                    // marker click — the double-click-consume path below (whose
                    // first click already landed, leaving the second land a
                    // same-sample no-op) and the plain-select path.
                    land_playhead_on_marker(app, audio, viewport, hit);
                    // THE CLICK OWNS ITS CLEAR (architect 2026-07-29): a
                    // single-select click moves the playhead onto a marker, so
                    // any resting scratch span ends here — unconditionally, and
                    // whether or not the land moved anything. A
                    // re-click of the already-selected marker therefore clears
                    // a resting highlight too; that is the
                    // ruling and not an accident (the click says "the playhead is
                    // HERE, at this point"). The double-click-consume path rides
                    // this same site, so an editor open through it finds no span.
                    clear_region_highlight(app, viewport);
                    // Double-click: a Marker candidate for the SAME index within
                    // the window opens the flag editor, exactly like Enter on the
                    // focused marker (the click above already single-selected it).
                    // The surface + target tag prevents any trim-bar / editor
                    // candidate from consuming here, and a candidate seeded on
                    // one part consumes on the other — one surface. Read-only,
                    // P view (phase resets have no per-flag editor), and the
                    // off-home column (active_column_authoring_allowed false —
                    // the warp editor is source-view-only) refuse silently,
                    // matching Enter's allowlist / view refusal — the candidate
                    // is cleared and the press stays a plain second select. On a
                    // consumed open nothing is armed and no fresh candidate
                    // seeds (the editor now owns input).
                    bool opened_editor = false;
                    const DoubleClickCandidate& dc = dc_at_press;
                    if (dc.surface == DoubleClickSurface::Marker &&
                        dc.target == hit &&
                        monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                        std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                        std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                        if (app.active_markers_view != 'P' &&
                            !active_view_state(app).read_only &&
                            active_column_authoring_allowed(app)) {
                            // Every open route opens fully SELECTED (open-
                            // selected), so there is no clicked-glyph caret to
                            // seat — a press on the flag BOX and a press on the
                            // marker's STEM are the same open. A specific caret
                            // spot is a click inside
                            // the already-open editor (the F2.1 path).
                            flag_editor.enter_top_flag_edit(hit);
                            opened_editor = true;
                        }
                    }
                    if (!opened_editor) {
                        // SEED a Marker candidate at this PRESS — the one seed
                        // timing for the whole marker surface (the release-time
                        // seeding is retired). Press-seeding is safe for the
                        // flag part because a press that becomes a real drag
                        // drops the candidate at the threshold crossing (see
                        // on_motion's pending-marker-drag branch).
                        app.double_click = DoubleClickCandidate{
                            .surface = DoubleClickSurface::Marker,
                            .time_ms = monotonic_ms(),
                            .press_x = x, .press_y = y,
                            .target  = hit};
                        // A writable tab arms the pending REPOSITION drag on a
                        // plain marker press IN THE COLUMN'S HOME VIEW only —
                        // from EITHER surface, the flag box or its stem
                        // (2026-08-01): the drag tracks the pointer's x from the
                        // press point, which is the same gesture whichever the
                        // press started on. The old on_flag half of this test
                        // went with the marker-text lane's run in row 5;
                        // read-only selects but never arms (marker
                        // mutation refused). ONE DRAG, ONE GATE since 2026-07-29:
                        // the home-view split that used to arm the TEMPO drag
                        // instead in W view + TARGET view exactly — the pointer
                        // half of the home-view binding's tempo exception, with its
                        // predecessor-eligibility walk — is DELETED with that whole
                        // gesture (see marker_drag.h). So EVERY off-home flag press
                        // (W+target and P+source alike) selects and LANDS the
                        // playhead but arms nothing at all — the silent
                        // navigation-class refusal, marker motion being authoring.
                        if (!active_view_state(app).read_only &&
                            active_column_authoring_allowed(app)) {
                            app.pending_marker_drag = PendingMarkerDrag{};
                            app.pending_marker_drag.active  = true;
                            app.pending_marker_drag.marker  = hit;
                            app.pending_marker_drag.press_x = x;
                            app.pending_marker_drag.press_y = y;
                        }
                    }
                }
            } else {
                // Empty top-strip spot — no marker flag under the point (the
                // trim bar already returned above; mh_index < 0 here).
                // ONE lane to test: the flag and triangle lanes became the
                // single marker lane in row 5.
                const GuiRect marker_lane = top_marker_row_area(app);
                const bool in_flag_or_tri =
                    y >= marker_lane.y && y < marker_lane.y + marker_lane.h;
                if (in_flag_or_tri && !shift) {
                    // The empty marker-lane parity press (architect
                    // 2026-07-23): the empty lane works like the waveform upper half. A
                    // DOUBLE-CLICK consume creates a marker at the clicked
                    // position — the AUGMENTED drop, the same and only drop bare
                    // `s` performs (architect 2026-07-28: the lane double-click
                    // reuses the keyboard's machinery, so it follows it); the
                    // FIRST press seeds the candidate AND runs the placement body
                    // (deselect + playhead + region arm).
                    // PLAIN ONLY: a SHIFT press on the lane claims nothing at all
                    // and falls to the inert return below, exactly like the
                    // SHIFT-exact trim-bar press — shift has no meaning here (the
                    // waveform's shift region former does not extend to the lane),
                    // so it seeds nothing, places nothing, and leaves a shift+drag
                    // inert. Ctrl and alt never reach this branch (discarded at the
                    // strict-modifier gate above).
                    // NO STOP HERE, deliberately, and this is the one ACTING
                    // top-strip press that omits one: PARITY is the whole reason —
                    // a live session RESEEKS to the placed playhead
                    // (place_playhead_and_arm_region's was_playing arm) exactly as
                    // it does on the waveform upper half, so a double-click drop
                    // lands over a live session without cutting it off. Do not add
                    // a stop to make this branch look like its siblings — the
                    // omission IS the ruling.
                    const int click_rel_x = x - area.x;
                    const DoubleClickCandidate& dc = dc_at_press;
                    if (dc.surface == DoubleClickSurface::EmptyLane &&
                        monotonic_ms() - dc.time_ms <= kDoubleClickMs &&
                        std::abs(x - dc.press_x) <= kDoubleClickSlackPx &&
                        std::abs(y - dc.press_y) <= kDoubleClickSlackPx) {
                        create_marker_at_empty_lane(click_rel_x);
                        return;
                    }
                    // SEED an EmptyLane candidate at this PRESS (position-keyed;
                    // target unused). Cleared at the region-drag threshold
                    // crossing so a moved drag never carries one (on_motion).
                    app.double_click = DoubleClickCandidate{
                        .surface = DoubleClickSurface::EmptyLane,
                        .time_ms = monotonic_ms(),
                        .press_x = x, .press_y = y,
                        .target  = -1};
                    place_playhead_and_arm_region(
                        click_rel_x, x, y, was_playing, playhead_at_entry);
                    return;
                }
                // Every other empty top-strip spot: NOTHING AT ALL — no
                // playhead, no marker, no selection or region change, and no
                // playback effect either, because this press claimed nothing
                // and the stops all live at the claims. That covers the whole
                // marker lane outside every flag box — a box under the point is
                // a marker hit and never reaches this branch. The inter-lane
                // gaps, the SHIFT-exact trim-bar press and the SHIFT-exact
                // marker-lane press land here too, equally inert.
                return;
            }
            return;
        }

        // Waveform-area press: marker-blind for SELECTION (it never SELECTS a
        // hit marker — the stem is not a grab target; hit_test_flag runs only
        // for top-strip presses). The PLAIN press splits by HALF
        // (architect 2026-07-23): the UPPER half keeps the placement press —
        // CLEARS the selection (the deselect-all: a waveform click dismisses
        // the marker selection, the Ableton behaviour), drops the playhead at
        // the clicked column (no marker snap — the 3px marker-snap magnet
        // already died with the retired plain-drag scrub), reseeks a live
        // scanner to it, overrides follow, and arms the region drag — which
        // also DISSOLVES any resting highlight at this mouse-down
        // (arm_region_drag_at clears it), so the highlight vanishes on press
        // whether the gesture becomes a click or a fresh drag. The LOWER half
        // is the SCRUB surface (the branch below): the press runs one
        // scrub act — stopping a live session, else starting a fresh SCANNER
        // session from the clicked
        // frame — arming nothing (the one-shot Ableton model) and
        // touching nothing else. ONLY the plain press splits — a Shift press
        // instead FORMS a region waveform-wide (the drop-and-span path below), never
        // a plain press's playhead placement, and ctrl/alt already claimed
        // their waveform-wide gestures above.
        {
            const int click_rel_x = x - area.x;
            if (shift) {
                // Waveform shift+click: the region former, ONE OF THE TWO
                // formers there are (this and the plain upper-half drag) and,
                // like its sibling, it CLEARS THE SELECTION — which is what makes
                // "a region rests only beside an empty selection" structural
                // (the inventory is at RegionState, app_state.h; architect
                // 2026-07-23, replacing the reserved strict no-op). With NO markers
                // selected it forms a region from the PLAYHEAD to the clicked
                // column; with markers selected the selection DROPS and the
                // region spans from the selected marker FURTHEST
                // from the click to the click (one rule: furthest =
                // argmax |pos - click| over the selection in active-domain
                // frames, which covers the between-the-series case as the
                // longest side). It moves NO playhead, stops NO playback,
                // reseeks nothing, overrides no follow, seeds no double-click.
                // Read-only allowed (the region is
                // transient navigation; the drop's deselect is selection =
                // navigation). ctrl/alt returned earlier, so this is
                // shift-exact — a shift+modified combination never reaches here.
                // The deselect runs FIRST, before the gutter early-return, so
                // an inert-gutter shift+click (no column to form a region from)
                // still drops the marker selection — every waveform click drops
                // it, mirroring the plain branch's gutter clear.
                //
                // NEW (architect 2026-07-24 second pass): the former now also
                // ARMS a region drag anchored at the FAR endpoint (the playhead,
                // or the drop's furthest-marker image). So the press is
                // one-shot ONLY on a motionless release: the formed region rests
                // exactly as today, and on the sliver rule whatever the deselect
                // below left standing does; motion past the shared gate drags the
                // CLICK-side endpoint live through the region-drag motion path,
                // the far endpoint fixed, and Esc mid-drag does NOTHING AT ALL
                // (pointer gestures have no cancel, 2026-07-29 — the rule at the
                // drag-modal gate): the drag keeps extending and its release rests
                // the span it grew. GUTTER presses arm
                // NOTHING (there is no column to anchor a drag against).
                if (click_rel_x < 0 || click_rel_x >= area.w) {
                    selection.clear_selection();
                    return;
                }
                // Region endpoints hold PLAYABLE live-domain frames only: the
                // display-state validator (clamp_display_state_to_live_domain)
                // defines an endpoint >= total as invalid and clears the whole
                // highlight, and the forward map rounds unclamped, so an EOF
                // item's image (a marker at total-1 under a fast map) can land
                // one past the wall — clamping here through the land's own
                // helper keeps every former inside the one region domain. The
                // click_frame is clamped for the same conformance (the plain
                // press path clamps it through move_playhead_to; the region
                // former stored it raw).
                const int64_t click_frame = clamp_playhead_to_live_domain(
                    playhead_frame_at_click_column(app, audio, click_rel_x),
                    app, audio);
                int64_t endpoint = app.playhead_cursor_sample;
                if (!app.selected_markers.empty()) {
                    // Drop: the region's far endpoint is the selected marker
                    // whose active-domain position is furthest from the click.
                    // Stale indices are skipped defensively; if every index was
                    // stale (degenerate) the playhead endpoint stands.
                    int64_t best_dist = -1;
                    for (int idx : app.selected_markers) {
                        int64_t src_frame;
                        if (app.active_markers_view == 'P') {
                            const auto& tv = app.phaseresetmarkers.markers();
                            if (idx < 0 || idx >= static_cast<int>(tv.size()))
                                continue;
                            src_frame = tv[idx].time_frame;
                        } else {
                            const auto& mv = app.warpmarkers.markers();
                            if (idx < 0 || idx >= static_cast<int>(mv.size()))
                                continue;
                            src_frame = mv[idx].time_frame;
                        }
                        // Clamp the forward-map image into the live domain (see
                        // the click_frame comment above): an EOF marker's image
                        // can round to total, which the display-state validator
                        // rejects — the land's own helper keeps this endpoint a
                        // playable frame.
                        const int64_t pos = clamp_playhead_to_live_domain(
                            source_frame_to_active_domain(app, audio, src_frame),
                            app, audio);
                        const int64_t dist = pos > click_frame
                                                 ? pos - click_frame
                                                 : click_frame - pos;
                        if (dist > best_dist) {
                            best_dist = dist;
                            endpoint  = pos;
                        }
                    }
                    // Deselect — the selection drops and the span forms beside an
                    // empty selection (the shift-click waveform former drops the
                    // selection by explicit ruling, exactly as the plain drag's
                    // press does). This also dissolves the shift-range anchor,
                    // correct here: this shift interaction is on a DIFFERENT
                    // surface (the waveform) than the marker range select, so no
                    // range is being extended.
                    selection.clear_selection();
                }
                // Sliver rule (mirrors end_region_drag_min_size_check): a span
                // narrower than the drag threshold — a click at the playhead,
                // hand jitter — leaves no region window.
                const double spp = current_samples_per_pixel(app, audio);
                if (spp <= 0.0) return;
                if (std::abs(static_cast<double>(endpoint - click_frame)) / spp
                        < kDragMovedThresholdPx) {
                    // Sliver: form NO region — app.region is untouched HERE, so
                    // a motionless release keeps whatever the deselect above left
                    // standing (a pre-press scratch span rests bit-for-bit; the
                    // deselect above touches no region at all). An Esc
                    // mid-drag from here is a consumed no-op like everywhere else
                    // (no cancel), so the drag simply continues. ALSO
                    // ARM the drag anchored at the far endpoint so dragging out
                    // past the gate still grows a fresh region live from it (e.g.
                    // shift-click AT the playhead, then drag out).
                    arm_region_drag_preserving(endpoint, x, y);
                    return;
                }
                // RegionState endpoints are unordered — the painter and Space
                // normalize lo/hi. Damage the waveform (the region paints as a
                // direct overlay), matching the region-drag extend.
                app.region.active     = true;
                app.region.a_frame    = endpoint;
                app.region.b_frame    = click_frame;
                // ORDER: the deselect above ran FIRST, so this span rests beside
                // an EMPTY selection — one of the region's two formers, both of
                // which deselect at press (the inventory is at RegionState,
                // app_state.h).
                viewport.invalidate_waveform_area();
                // ARM the drag anchored at the FAR endpoint, PRESERVING the
                // just-formed region: a motionless release keeps it (today's
                // one-shot region, bit-for-bit), motion past the gate drags the
                // click-side endpoint live (the motion handler fixes a_frame =
                // anchor = endpoint and tracks b_frame to the pointer).
                arm_region_drag_preserving(endpoint, x, y);
                return;
            }
            // THE HALF TEST, first thing on the plain path — before the
            // deselect, because a scrub press must not deselect. Lower half of
            // the waveform area (waveform_lower_half, which the SCRUB CURSOR's
            // zone predicate reads too — one owner, so the cue and the gesture
            // cannot disagree about where the surface starts) = the SCRUB
            // surface: the press drives the SCANNER (not the cursor) — the
            // scanner fields
            // are meaningful only while active (the standing contract), and
            // this gesture is exactly the launches-the-scanner-independently-
            // of-the-cursor consumer that contract anticipated. Every scrub
            // act is STOP-THEN-START-ON-THE-NEXT-CLICK (scrub_act_at, architect
            // 2026-07-27): a click over a LIVE session just stops it, and the
            // following click launches a fresh audition from the frame it lands
            // on — never a positional seek inside an old session. A refused
            // launch is a silent no-op, exactly Space's conventions. The scrub
            // is ONE-SHOT per click (architect 2026-07-23, the Ableton model):
            // the press arms NOTHING, a held press does nothing further, and
            // motion over the surface is inert — each click pays at most one
            // stop quiescence fence. It touches
            // NOTHING else: no selection change, no region change, no cursor
            // write, no follow override, no double-click seed, no drag
            // arm. Read-only allowed (playback is navigation). The gutter
            // (click_rel_x outside [0, area.w)) returns silently — no launch
            // position exists there, unlike the upper half's
            // deselect-then-return.
            if (waveform_lower_half(area, y)) {
                // The scrub press body (scrub_press_at): gutter no-op, clamped
                // frame from the column, one scrub act (stop a live session,
                // else launch), nothing armed. This is the LEFT button's entry
                // — the waveform lower half; the bare right press over the full
                // waveform height is the body's other caller (its own comment
                // is above, in this same handler).
                scrub_press_at(click_rel_x);
                return;
            }
            // Upper half: the placement press body, shared verbatim with the
            // empty flag/triangle-lane parity press
            // (place_playhead_and_arm_region): deselect-all, drop the playhead
            // at the clicked column, reseek a live session, override follow, arm
            // the region drag. The clear runs FIRST inside the helper, so an
            // inert-gutter click still deselects.
            place_playhead_and_arm_region(click_rel_x, x, y,
                                          was_playing, playhead_at_entry);
        }
    }
    // Wheel events no longer reach on_button_press; they arrive coalesced
    // per pointer frame through on_wheel -> handle_wheel.
}

void GuiInputHandler::finalize_editor_text_drag() {
    const ActiveEditorText g = active_editor_text(app, audio);
    if (g.valid) {
        // A press that never moved leaves a plain caret and no selection,
        // matching the existing click-to-caret.
        if (g.ed->selection_anchor == g.ed->cursor_pos)
            g.ed->selection_anchor = -1;
        if (g.bottom_strip) viewport.invalidate_timestamp_area();
        else                viewport.invalidate_top_strip();
    }
    app.editor_text_drag.active = false;
}

void GuiInputHandler::arm_region_drag_at(int64_t anchor_frame, int x, int y) {
    app.region_drag = RegionDragState{};
    app.region_drag.active       = true;
    app.region_drag.anchor_frame = anchor_frame;
    app.region_drag.press_x      = x;
    app.region_drag.press_y      = y;
    // Clear any resting region immediately at press: a plain upper-half
    // waveform press dissolves an existing highlight on mouse-down (the plain
    // canvas ground repaints back now, not at release; a lower-half scrub press never
    // reaches here — it arms no region drag and leaves the region alone). A
    // moved drag rebuilds a fresh region live; a
    // motionless press-release simply leaves it cleared, and an Esc mid-drag
    // changes nothing at all (no cancel) — the dissolve at mouse-down is final
    // either way. Same dissolve shape as
    // the navigation clears, so it shares clear_region_highlight.
    clear_region_highlight(app, viewport);
}

void GuiInputHandler::arm_region_drag_preserving(int64_t anchor_frame, int x,
                                                 int y) {
    // The SHIFT-exact former's arm (labwc 2026-07-24 second pass). Same drag
    // state as arm_region_drag_at — active, anchored at the FAR endpoint (the
    // playhead, or the drop's furthest-marker image), press coordinates for
    // the shared Chebyshev gate — but it does NOT dissolve app.region: the
    // former has already left it exactly as it should REST for a motionless
    // release (the freshly formed region, or on the sliver rule the pre-press
    // region untouched), so preserving it keeps today's one-shot behaviour
    // bit-for-bit. That no-dissolve-at-press property is this function's whole
    // reason to exist; neither arm has a cancel to differ in — Esc mid-drag is a
    // consumed no-op for both. Past the gate the SHARED region-drag motion
    // handler re-establishes
    // app.region from this anchor (it fixes a_frame = anchor_frame and tracks
    // b_frame to the pointer column on each column change), so the click-side
    // endpoint drags live while this far endpoint stays put — no motion or
    // release handler change needed, the anchor semantic is identical to the plain
    // drag's.
    app.region_drag = RegionDragState{};
    app.region_drag.active       = true;
    app.region_drag.anchor_frame = anchor_frame;
    app.region_drag.press_x      = x;
    app.region_drag.press_y      = y;
}

void GuiInputHandler::place_playhead_and_arm_region(int click_rel_x, int x,
                                                    int y, bool was_playing,
                                                    int64_t playhead_at_entry) {
    // The waveform-upper-half placement press body, shared by the plain waveform
    // press and the empty flag/triangle-lane parity press. The clear runs
    // FIRST, before the gutter early-return, so an inert-gutter click (no column
    // to seat a playhead) still deselects.
    const GuiRect area = waveform_area(app);
    selection.clear_selection();
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    // Clamp the click column's frame into the live domain ONCE and pass that
    // same clamped value to both the playhead move and the region arm:
    // move_playhead_to clamps internally, but the region former stored the raw
    // value. At a fractional flush-right zoom the painter-quantized wall
    // (q = nearbyint(spp*W)/W) differs from the click conversion's
    // current_samples_per_pixel, so the last visible column's frame can compute
    // to domain_total — one past [0, domain_total-1], which the display-state
    // validator would clear wholesale — so both formers clamp.
    const int64_t sample = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    viewport.move_playhead_to(sample);
    if (was_playing && sample != playhead_at_entry)
        playback_lifecycle.reseek_keeping_alive(sample);
    // SUPPRESS THE CHASE for this session: the user placed the cursor
    // deliberately, so follow must not page the viewport away from it. One of
    // two producer classes (the other being any viewport pan); the inventory and
    // the clearing rule live at the flag's declaration, app_state.h.
    if (was_playing) app.follow_overridden_for_session = true;
    arm_region_drag_at(sample, x, y);
}

void GuiInputHandler::create_marker_at_empty_lane(int click_rel_x) {
    // The empty flag/triangle-lane double-click marker create: the bare-`s` drop
    // equivalent, and like bare `s` it is the AUGMENTED drop in both columns —
    // the copy-previous owner in W, the lead-in reset in P. Gated exactly like
    // the keyboard `s`: home-view (active_column_authoring_allowed) and read-only
    // refuse SILENTLY. Place the playhead on the clicked column first — the
    // double-click's first press already moved it there, so this is a harmless
    // same-value repeat that also covers any first press whose placement was
    // undone in between. The standing _at_playhead drops then author at that
    // playhead, taking the full create path (walls, undo, selection, and the
    // point command's own region collapse at the drop chokepoint) — the
    // phase-reset lead-in additionally offset N/2 before the playhead and landing
    // the playhead per that drop's rule.
    const GuiRect area = waveform_area(app);
    if (click_rel_x < 0 || click_rel_x >= area.w) return;
    if (active_view_state(app).read_only) return;
    if (!active_column_authoring_allowed(app)) return;
    const int64_t sample = clamp_playhead_to_live_domain(
        playhead_frame_at_click_column(app, audio, click_rel_x), app, audio);
    viewport.move_playhead_to(sample);
    // active_column_authoring_allowed guarantees the column is in its HOME audio
    // view: W -> source (warp drops legal), P -> target (phase-reset drops legal,
    // the lead-in's target requirement satisfied), so the view dispatch below
    // needs no extra audio-view guard.
    if (app.active_markers_view == 'P')
        phase_resets.drop_phase_reset_lead_in_at_playhead();
    else
        warpops.drop_copy_previous_at_playhead();
}

void GuiInputHandler::on_button_release(GuiMouseButton button, int x,
                                        int y, GuiInputState /*mods*/) {
    // THE DROPDOWN'S RELEASE, above every gate: while it is open it owns the
    // pointer, and its items are the redesign's one act-on-release surface.
    if (button == GuiMouseButton::Left && app.dropdown.open()) {
        if (finish_dropdown_release(x, y)) return;
    }
    // THE CLICK FACE ENDS WITH THE PHYSICAL HOLD, above every gate below: a
    // prompt opened by the press (or any other early return) must not strand a
    // lit interior on a button nobody is pressing any more. Nothing else about
    // the redesigned rows happens at a release — they have no release body.
    if (button == GuiMouseButton::Left) clear_redesign_button_press();
    if (app.prompt.active) return;
    // F2.1: a left release ending an editor-text drag finalizes the
    // selection (or collapses to a caret) before the modal swallow below.
    if (button == GuiMouseButton::Left && app.editor_text_drag.active) {
        const ActiveEditorText g = active_editor_text(app, audio);
        finalize_editor_text_drag();
        // Double-click seeding: a MOTIONLESS release (a pure click that left a
        // caret, no selection) seeds an editor-text candidate so a second click
        // within the window selects the clicked character class's run (word /
        // punctuation / whitespace). A drag that made a selection seeds nothing.
        if (g.valid && !text_editor::has_selection(*g.ed)) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::EditorText,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
        return;
    }
    if (text_editor::is_active(app.settings_editor)) return;
    if (text_editor::is_active(app.commit_editor)) return;
    // NON-LEFT RELEASES END HERE, and nothing is owed: every release body below
    // finishes something a LEFT press armed, and no other button arms anything.
    // The bare RIGHT press bound 2026-08-01 is a one-shot scrub act — it arms no
    // drag, seeds no double-click candidate and lights no button face — so its
    // release is a pure no-op by construction, not by an omission to fix later.
    if (button != GuiMouseButton::Left) return;

    // THE TRIM-BAR FRAMING DOUBLE-CLICK'S SEED, resolved for every left release
    // because only the release can tell a click from a drag. The press recorded
    // the trim-bar point (TrimBarPressSeed); this seeds the candidate when the
    // pointer never left the slack AND no trim drag went live — the two spellings
    // of "it stayed a click", equal by construction (kDoubleClickSlackPx ==
    // kDragMovedThresholdPx). A moved endcap/bridge drag therefore seeds nothing
    // and, its own press having cleared any candidate at the top-of-frame, leaves
    // none behind. The record is consumed either way.
    {
        const TrimBarPressSeed seed = app.trim_bar_press;
        app.trim_bar_press = TrimBarPressSeed{};
        if (seed.active && !app.trim_drag.active &&
            std::abs(x - seed.press_x) <= kDoubleClickSlackPx &&
            std::abs(y - seed.press_y) <= kDoubleClickSlackPx) {
            app.double_click = DoubleClickCandidate{
                .surface = DoubleClickSurface::TrimBar,
                .time_ms = monotonic_ms(), .press_x = x, .press_y = y,
                .target = -1};
        }
    }

    if (app.strip_drag.active) {
        // Terminating event: if the drag moved, run the final apply with
        // final=true and the one synchronous rebuild (resync + kick_waveform_sync,
        // inside apply_strip_drag_zoom's final path) so the rest state is exact. A
        // motionless press-release finalizes nothing — and commits nothing
        // either: the ctrl-waveform selection clear is RETIRED (architect
        // 2026-07-23), the press being purely the strip drag. A drag that MOVED
        // additionally clears any pending double-click candidate, so a drag can
        // never supply the second click of one.
        if (app.strip_drag.moved) {
            apply_strip_drag_at(x, y, /*final_event=*/true);
            app.double_click = DoubleClickCandidate{};
        }
        app.strip_drag = StripDragState{};
        // reappear the cursor at the anchor-stem column (y frozen at the press
        // row) — the restore x override the drag set each event.
        end_strip_pointer_capture();
        return;
    }
    if (app.scroll_drag.active) {
        // Alt+drag grab-pan end: the pan applied incrementally during motion, so
        // there is nothing to finalize but the predictor. The continuous pan
        // deferred per-event resyncs, so re-anchor the predictor once here. The
        // pan captured the pointer at its arm, so end the capture (reappear the
        // cursor at the raw traveled virtual_pointer_x_, y frozen at the press
        // row — the pan sets no anchor-stem override); idempotent, so a degraded
        // compositor that never captured is unharmed.
        if (playback.is_playing()) playback.resync_predictor();
        app.scroll_drag = ScrollDragState{};
        end_strip_pointer_capture();
        return;
    }
    // (No scrub branch: the scrub is one act at the PRESS — it arms nothing,
    // so its release is an ordinary fall-through.)
    if (app.region_drag.active) {
        // The region is extended live during the drag (see on_motion); a drag
        // that moved rests the region at its final extent. A MOTIONLESS
        // press-release (never crossed the threshold) needs no collapse here:
        // the press already cleared any resting highlight at mouse-down (see
        // arm_region_drag_at), so a plain click leaves the region cleared and
        // there is nothing to do at release but disarm. A jitter drag that
        // crossed the gate but rests a sub-threshold sliver dissolves like a
        // click (end_region_drag_min_size_check) — but ONLY a MOVED drag runs
        // that check: the SHIFT-exact former's
        // preserving arm (arm_region_drag_preserving) does NOT dissolve
        // app.region, so a motionless shift-sliver press-release rests a legal
        // narrow region (the pre-press span, NOT subject to the
        // drag-rest minimum), and an
        // unconditional min-size check would delete
        // it. Capture `moved` BEFORE the state reset (the reset zeroes it). Plain
        // path unaffected: a plain MOVED drag still checks; a plain motionless
        // release left the region cleared at arm (arm_region_drag_at), so the
        // check would early-return anyway — the gate changes nothing there.
        const bool moved = app.region_drag.moved;
        app.region_drag = RegionDragState{};
        if (moved) end_region_drag_min_size_check(app, audio, viewport);
        return;
    }
    // (No tempo-drag arms here: the target-view tempo drag and its pending are
    // DELETED, architect 2026-07-29 — the tempo surface is the bare Up/Down cent
    // step alone. See marker_drag.h.)
    if (app.trim_drag.active) {
        commit_trim_drag();
        return;
    }
    if (app.pending_trim_drag.active) {
        // The pending trim drag never crossed the threshold: a motionless
        // endcap/bridge press, which is now a CONSUMED NOTHING (architect
        // 2026-07-30). Its whole act was publishing the trim window as a region
        // highlight, and that coupling retired with the SPAN FORM; a deselect
        // and a playback stop left behind would be pure cost on a click that
        // commits nothing. Just disarm. (A crossed pending became app.trim_drag
        // and commits through the branch above, where the setter's deselect and
        // the trim-mutation stop live.)
        app.pending_trim_drag = PendingTrimDrag{};
        return;
    }
    if (app.pending_marker_drag.active) {
        // The pending marker drag never crossed the threshold: a pure flag click.
        // The press already single-selected its marker, landed the playhead,
        // collapsed any resting span AND seeded the Marker double-click candidate
        // (press-time seeding), so there is nothing to commit — just disarm. The
        // DEFERRED arm that used to complete a held-back click here died with the
        // group drag (architect 2026-07-29: horizontal movement is a focus act, so
        // a press on a selected member single-selects at press time like any other
        // — the doctrine is at the head of position_nudge.h).
        // (A crossed pending became app.drag — dropping the candidate at the
        // threshold crossing — and commits through the branch below.)
        app.pending_marker_drag = PendingMarkerDrag{};
        return;
    }
    if (!app.drag.active) return;
    marker_drag.commit_drag();
}

// END every in-flight pointer gesture THROUGH ITS OWN RELEASE BODY. The one
// force-end route in the product: the Ctrl+Q hatch in the drag-modal gate
// (input_handler.cpp, where the no-cancel rule is stated in full) and main.cpp's
// resize / WM-close callbacks. It replaced cancel_active_drags — POINTER GESTURES
// HAVE NO CANCEL (architect 2026-07-29), so there is nothing to restore anywhere
// here: every live gesture COMMITS what stands, and undo is the mitigation.
// Homed beside on_button_release deliberately — these are the same bodies in the
// same order, and the region arm needs this TU's file-local min-size helper.
// The gestures are mutually exclusive in practice, so this reads as a chain of
// no-ops around the one that is live.
void GuiInputHandler::finalize_active_drags() {
    // Editor text-selection drag: FINALIZE (collapse a no-motion anchor to a
    // caret), the same act its release performs — selection-only, nothing to
    // revert. It seeds NO double-click candidate here: a force-end is not a click
    // (the clean release owns that seeding). The keyboard's own escape hatch for
    // this drag lives in on_key and is editor modality, not a gesture cancel.
    if (app.editor_text_drag.active) finalize_editor_text_drag();
    // The marker reposition drag commits its PROPOSED positions (the overlay
    // becomes the store) and pushes the one undo entry iff the drag netted a
    // change — the release path exactly, so Ctrl+Z reverts it.
    if (app.drag.active) marker_drag.commit_drag();
    // (The tempo drag's finalize arm went with the gesture, 2026-07-29 — see
    // marker_drag.h.)
    // The trim drag keeps its live bounds and runs the full commit tail (the
    // release column-snap, auto_clear_crossed_trim, the repaint/trigger, the
    // setter's deselect). TRIM IS HISTORY-LESS BY RULING, so these bounds are not
    // undoable — that is the standing trim gap (every trim gesture commits
    // outside undo), not something this force-end introduces.
    if (app.trim_drag.active) commit_trim_drag();
    if (app.region_drag.active) {
        // The region was extended live; the release rests it where it is, with the
        // sliver dissolve applying to a MOVED drag only (see the release arm).
        const bool moved = app.region_drag.moved;
        app.region_drag = RegionDragState{};
        if (moved) end_region_drag_min_size_check(app, audio, viewport);
    }
    if (app.strip_drag.active) {
        // Navigation gestures applied their motion continuously, so ending is just
        // ending: the one synchronous rebuild + predictor resync a moved drag's
        // release performs, then the capture release (idempotent).
        if (app.strip_drag.moved) {
            if (playback.is_playing()) playback.resync_predictor();
            viewport.kick_waveform_sync();
        }
        app.strip_drag = StripDragState{};
        end_strip_pointer_capture();
    }
    if (app.scroll_drag.active) {
        // Alt+drag grab-pan: incremental too, so re-anchor the predictor once and
        // end the capture, its release's whole body.
        if (playback.is_playing()) playback.resync_predictor();
        app.scroll_drag = ScrollDragState{};
        end_strip_pointer_capture();
    }
    // THE PENDINGS DISARM, and that is not a cancel: a pending has committed
    // NOTHING of its own — the marker pending's click committed at the
    // PRESS, and the bound-set trim pending's bound was written at the press and
    // stands. There is no release here (the button is still held), so nothing is
    // owed; a pending otherwise resolves only by the threshold crossing or a real
    // release / button loss. (TWO pendings, not three, since the tempo drag's
    // deletion — 2026-07-29, see marker_drag.h.)
    // THE ASYMMETRY WITH THE DRAGS ABOVE — live gestures COMMIT here while pendings
    // merely DISARM — is ARCHITECT-ACCEPTED (2026-07-29: "not a real use case - do
    // whatever is easiest to code and has least loopholes"). It is also the form
    // with the fewest loopholes: a live gesture has produced state that must land
    // somewhere, and a pending has produced none, so each does the only thing it
    // can. Do not add a completion arm here to make the two look alike.
    app.pending_marker_drag = PendingMarkerDrag{};
    app.pending_trim_drag   = PendingTrimDrag{};
    // A force-end is not a clean click sequence, so no candidate may survive to
    // pair with a later click (the standing rule at every non-release gesture
    // end) — and neither may the trim bar's press record, which would otherwise
    // seed one at the next release.
    app.double_click   = DoubleClickCandidate{};
    app.trim_bar_press = TrimBarPressSeed{};
}

// THE REDESIGNED BUTTONS' HOVER, in ONE transition writer over the whole roster
// (row 1's Quit / Navigation / Settings and the view bar's three, row 2's
// four, row 3's two tabs and row 4's eleven — the stash is
// AppState::redesign_buttons).
// A face changes only when its boolean does, and a motion that changes ANY of
// them pays exactly ONE invalidate_top_strip — the strip idiom (no narrow rects;
// the playhead columns' carve-out stays the sole exception), which also makes
// the common transition (leaving one button for its neighbour, two booleans
// flipping) cost the same single damage as any other. The rects are the
// painter's stashes, so a hovered region is the painted button and nothing is
// measured here.
void GuiInputHandler::clear_redesign_button_hover() {
    bool changed = false;
    for (AppState::RedesignButtonFace& f : app.redesign_buttons) {
        if (!f.hovered) continue;
        f.hovered = false;
        changed = true;
    }
    if (changed) viewport.invalidate_top_strip();
}

void GuiInputHandler::recompute_redesign_button_hover() {
    const int mx = app.last_mouse_x;
    const int my = app.last_mouse_y;
    bool changed = false;
    for (int i = 0; i < kRedesignButtonCount; ++i) {
        AppState::RedesignButtonFace& f = app.redesign_buttons[i];
        // A zero-width stash (before that row's first paint) contains no point,
        // and the pre-motion (-1, -1) cursor is outside every rect, so both cold
        // states resolve to "not hovered" without a special case.
        //
        // HOVERABILITY IS THE SECOND TERM (redesign_button_hoverable,
        // app_state.h): a DISABLED row-2 button and the SELECTED tab both refuse
        // the hover face, and both refusals live in that one predicate rather
        // than as conditions here or in the painter.
        const bool inside = app.pointer_in_window &&
                            rect_contains(f.rect, mx, my) &&
                            redesign_button_hoverable(
                                app, audio.total_frames(),
                                static_cast<RedesignButton>(i));
        if (f.hovered == inside) continue;
        f.hovered = inside;
        changed = true;
    }
    if (changed) viewport.invalidate_top_strip();

    // THE TOOLTIP'S DWELL STAMP, written here because this is the one place that
    // knows a hover STARTED. EVERY roster button but ROW 1'S carries a tooltip
    // (redesign_button_tooltip owns that membership, and no state changes it —
    // only Render's TEXT follows the iteration bit), so this walks the
    // whole roster: a newly hovered one stamps the clock, and moving between two
    // of them hides and re-stamps, so a fresh dwell begins on each arrival. The run
    // loop's tick compares the stamp against kTooltipDelayMs and flips
    // `visible` — no timer is created and nothing here decides visibility.
    //
    // NO DWELL RUNS UNDER A KEYBOARD-MODAL SURFACE OR A PROMPT, and this refusal
    // is what makes "a tooltip never floats over a modal" hold rather than merely
    // start out true. The HOVER PILL deliberately stays live under those surfaces
    // (the standing ruling: button hover is a pointer fact, and a lit pill
    // advertising a swallowed press is an accepted cost) — and both of the
    // branches that keep it live call this function, so without this line every
    // motion under a prompt or an editor would stamp a fresh dwell and the tick
    // would raise a FLOATING hint over the modal 700ms later. A hint is not a
    // face: it is a second surface, it hangs past the strip, and the chord it
    // names is exactly what the modal gate is swallowing. Forcing "no owner" here
    // rather than gating the tick keeps the stamp and the hide in one place — the
    // walk below then also hides whatever was already up, so the modal's OPEN
    // edge needs nothing beyond its own hide (on_key's, for the case where no
    // motion and no tick follow).
    // THE DROPDOWN NEEDS NO TERM HERE: redesign_button_hoverable refuses the
    // whole roster while a popup is up, so the walk finds no owner by itself.
    const bool modal_owns_the_keyboard =
        app.prompt.active || keyboard_modal_editor_active();
    int hovered_tip = -1;
    for (int i = 0; i < kRedesignButtonCount && !modal_owns_the_keyboard; ++i) {
        const RedesignButton id = static_cast<RedesignButton>(i);
        if (redesign_button_tooltip(app, id).line1 != nullptr &&
            app.redesign_buttons[i].hovered) {
            hovered_tip = i;
            break;
        }
    }
    if (hovered_tip < 0) {
        hide_shift_tooltip();
    } else if (app.redesign_tooltip.owner != hovered_tip) {
        // A DIFFERENT tooltip button (or the first one) — hide whatever was up
        // and start this button's dwell from zero. Keying on the id is what
        // makes a direct Render->Paste motion wait the full delay again instead
        // of inheriting the dwell that was already running.
        hide_shift_tooltip();
        app.redesign_tooltip.owner    = hovered_tip;
        app.redesign_tooltip.hover_ms = monotonic_ms();
    }
}

// THE OPEN DROPDOWN'S OWN HOVER, the pointer's only hover while it is up. One
// transition writer like the roster's, damaging the strip and the popup box on
// a change; the rects are the painter's published item boxes, so a highlighted
// item is exactly the box that lights and exactly the box a click hits. The
// closed menu's rects are zero and contain no point, so the walk needs no
// membership test beyond the open check.
void GuiInputHandler::recompute_dropdown_hover() {
    if (!app.dropdown.open()) return;
    const int mx = app.last_mouse_x;
    const int my = app.last_mouse_y;
    int hit = -1;
    for (int i = 0; i < dropdown_item_count(app.dropdown.menu); ++i) {
        const GuiRect& r = app.dropdown.item_rects[static_cast<size_t>(i)];
        if (rect_contains(r, mx, my)) {
            hit = i;
            break;
        }
    }
    if (app.dropdown.hovered_item == hit) return;
    app.dropdown.hovered_item = hit;
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(app.dropdown.rect);
}

// THE ONE CHORD-DISPATCH BODY for every redesigned button whose action IS a
// chord — rows 1 through 4, driven entirely by kToolbarChords' per-button flags
// so no row carries a special case of its own. Returns true when a button's rect
// claimed the press, whether or not anything was dispatched (a refusal is still
// a consumed nothing, which is what the band claims want).
//
// THE BUTTON IS ITS CHORD, dispatched through on_key: the action is not merely
// the same FUNCTION the key calls, it is the same ROUTE — every gate the chord
// passes on the keyboard (the loading/blank return, the keyboard-modal editor
// gate, the read-only allowlist, the arm's own refusals) applies here, in the
// same order, with nothing restated and nothing that can drift. It is the exact
// inverse of the platform's bare-`e`-as-left-button translation: one vocabulary
// expressed on the other's surface, at a boundary.
//
// The caller has already refused ctrl and alt at the band; shift arrives live
// and is decided per button here.
bool GuiInputHandler::dispatch_redesign_chord(int x, int y, GuiInputState mods) {
    for (const ToolbarChord& tc : kToolbarChords) {
        if (!redesign_button_hit(app, tc.id, x, y)) continue;
        // A SHIFT PRESS ON A BUTTON WITH NO SHIFTED CHORD is a consumed nothing
        // — never the unshifted action, which would be a silent lie about what
        // the modifier did (the flag's rationale is at its declaration).
        if (mods.shift && !redesign_button_shift_admits(tc.id)) return true;
        // A DISABLED BUTTON'S PRESS IS A CONSUMED NOTHING: the chord is not
        // dispatched at all, and a SHIFT press is swallowed exactly like the
        // plain one (one predicate, both routes — a greyed Render is greyed for
        // both of its chords). The predicate is the painter's
        // (redesign_button_enabled, app_state.h), so the greyed face and the
        // inert press are the same fact read twice and the press cannot slip
        // through on a frame the paint disagreed with. Dispatching anyway would
        // be harmless — every one of these chords refuses on its own — but it
        // would leave the disabled face lying about what a click does. Rows 1, 3
        // and 4 have no disabled face and the predicate is simply true there.
        if (!redesign_button_enabled(app, audio.total_frames(), tc.id))
            return true;
        // A RADIO ALREADY SELECTED HAS NOTHING TO SWITCH TO, and its chord is a
        // TOGGLE — dispatching would switch AWAY from what the user just
        // clicked. So the press is a consumed nothing, which also makes "no
        // button was hit" and "the selected half was hit" the same silent
        // outcome. Toggles (follow, iteration) are NOT radios and press through
        // in both directions.
        if (tc.radio && redesign_button_selected(app, tc.id)) return true;
        // THE CLICK FACE ARMS BEFORE THE ACTION RUNS, so a chord whose route
        // repaints the strip (undo, save, a view switch) paints the pressed
        // interior on the very frame it produces. It is cleared by the release /
        // the pointer-leave hook, never by the action. A SHIFT press takes it
        // too — it is the same physical hold, and the face tracks the hold.
        if (tc.click_face &&
            app.redesign_pressed != redesign_button_index(tc.id)) {
            app.redesign_pressed = redesign_button_index(tc.id);
            viewport.invalidate_top_strip();
        }
        // The shift term ORs the table's own (Redo's Ctrl+Shift+Z) with the
        // pointer's — well-defined because no row sets both (see shift_admits),
        // so this one expression spells both members of each shifted pair.
        GuiInputState chord{};
        chord.ctrl  = tc.ctrl;
        chord.shift = tc.shift || mods.shift;
        chord.alt   = tc.alt;
        on_key(tc.key, chord);
        return true;
    }
    return false;
}

// WHICH ITEM IS AT (x, y), or -1. The rects are the painter's published item
// boxes, so a hit is exactly the box that lights; a closed popup has zero rects
// and therefore contains no point, which is the correct cold answer.
int GuiInputHandler::dropdown_item_at(int x, int y) const {
    for (int i = 0; i < dropdown_item_count(app.dropdown.menu); ++i) {
        const GuiRect& r = app.dropdown.item_rects[static_cast<size_t>(i)];
        if (rect_contains(r, x, y)) return i;
    }
    return -1;
}

// THE DROPDOWN'S RELEASE — the one place in the redesign where an action fires
// on the button coming UP. Returns true when the release belonged to the popup
// and the caller must stop.
//
// RELEASE ON THE ARMED ITEM triggers it; release ANYWHERE ELSE (another item,
// the popup's chrome, outside the window) just drops the armed face and LEAVES
// THE POPUP OPEN — the menu convention, and the escape hatch for a press that
// landed on the wrong row. The outside-press close is untouched by this: that is
// a PRESS act and still closes and consumes, so the two rules do not overlap —
// a press outside never arms anything, so no release can be owed.
bool GuiInputHandler::finish_dropdown_release(int x, int y) {
    if (!app.dropdown.open()) return false;
    const int armed = app.dropdown.pressed_item;
    if (armed < 0) return true;   // a popup press that armed nothing; consumed
    const DropdownMenu menu = app.dropdown.menu;
    app.dropdown.pressed_item = -1;
    if (dropdown_item_at(x, y) != armed) {
        // Slid off: drop the face, keep the menu up.
        viewport.invalidate_top_strip();
        viewport.invalidate_rect(app.dropdown.rect);
        return true;
    }
    // CLOSE FIRST, THEN ACT — the popup is gone before anything the item does
    // runs, so a modal it opens never overlaps the menu even for a frame, and a
    // COMMAND it dispatches is not swallowed by the popup's own keyboard gate.
    // The two menus' actions differ in kind and each stays with its own table.
    if (menu == DropdownMenu::Navigation) {
        // THE ITEM IS ITS KEY, dispatched through on_key exactly as a redesigned
        // button dispatches its chord: every gate the keyboard route passes
        // (loading/blank, the modal gates, the read-only allowlist, the arm's own
        // refusals) applies identically, so an item whose command cannot act
        // right now simply does nothing — the buttons-never-grey rule, one
        // surface further out. No stop, no modal, nothing restated here.
        const NavigationPopupItem& it =
            kNavigationPopupItems[static_cast<size_t>(armed)];
        close_dropdown();
        GuiInputState chord{};
        chord.ctrl  = it.ctrl;
        chord.shift = it.shift;
        chord.alt   = it.alt;
        on_key(it.key, chord);
        return true;
    }
    // SETTINGS: the editor's open is its own ordinary route, prefilled through
    // the one recall serializer.
    const char* key = kSettingsPopupItems[static_cast<size_t>(armed)].key;
    close_dropdown();
    playback_lifecycle.stop_playback_for_modal_open();
    settings_editor.open_prefilled(key);
    return true;
}

// THE DROPDOWN'S TWO WRITERS. Both damage the same pair of rects —
// the top strip AND the popup's own published box — because the popup hangs
// BELOW the strip and overlaps the rows and the waveform under it, so strip
// damage alone would leave its overhang stale on the close. On the OPEN the box
// has not been painted yet (its rect is still zero), so the strip damage is what
// schedules the frame that paints it and publishes the rect; on the CLOSE the
// rect from the last paint is exactly the region to erase, which is why the
// close reads it BEFORE zeroing the state.
//
// EVERY CLOSE THROUGH HERE ALSO DISARMS THE MENU ROW
// (AppState::Dropdown::menu_row_armed): bare Esc, an item activating, the anchor
// click that closes its own menu, a press anywhere else, the wheel, Ctrl+Q, the
// WM close and a resize all end the mode through this one owner, because a
// dismissal the user MEANT must end the mode — or Esc would put away a menu that
// the next pointer twitch reopens.
// THE CLEAR SITS ABOVE THE "NOTHING IS OPEN" RETURN, and that placement is the
// whole point rather than a detail: the mode's defining state is menu CLOSED and
// row ARMED (what a row-1 hover close leaves behind), so a dismissal arriving in
// it finds nothing to close and must still go cold. Riding the whole-struct
// reset alone would have made every one of those routes a no-op in exactly the
// state the mode exists for. Below the return the reset re-clears the bit, which
// costs nothing and keeps the struct one initializer.
// THE ONE CLOSE THAT KEEPS THE MODE is the row-1 hover close in on_motion —
// sliding onto Quit or the view bar is a step ACROSS the bar, not a dismissal —
// and it re-arms on the line after its call to this. It is the only site in the
// tree that writes that bit true outside toggle_dropdown's open.
void GuiInputHandler::close_dropdown() {
    app.dropdown.menu_row_armed = false;
    if (!app.dropdown.open()) return;
    const GuiRect painted = app.dropdown.rect;
    app.dropdown = AppState::Dropdown{};
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(painted);
}

void GuiInputHandler::toggle_dropdown(DropdownMenu menu) {
    // ONE STATE, SO ONE MENU: a press on the OPEN menu's own button closes it
    // (the gesture that opened it, closing it), and a press on the OTHER menu's
    // button switches — the close below runs first, damaging the box that is
    // leaving, and the open then proceeds. "Two dropdowns are never open
    // together" needs no rule beyond this: the field holds one value.
    const bool same = (app.dropdown.menu == menu);
    close_dropdown();
    if (same) return;
    // OPENING A MENU ENDS AN ACTIVE FLAG EDIT, discarding it — exactly what a
    // press anywhere outside the editor's box already does. It is what keeps "a
    // popup and an editor are never open together" true, and the FLAG editor is
    // the one class that needs it: the three BOTTOM-STRIP modal editors (the
    // settings and render-commit editors and the BPM bracket, the membership
    // modal_bottom_strip_editor_active names) swallow every press at the top of
    // on_button_press, above the row-1 band claim, so a menu button is not even
    // reachable while one of them is up — but the FlagPayload editor is
    // pointer-TRANSPARENT by ruling and swallows nothing, so its edit would
    // otherwise stand under the open menu. That is not merely untidy: a settings
    // item opens the settings editor, on_key tests the flag editor FIRST, and
    // the typing would land in the flag buffer while the settings editor is what
    // looks focused.
    //
    // IT SITS ON THE OPEN PATH RATHER THAN AT THE BAND CLAIM because this is the
    // ONE route every open passes — the anchor click, the hover switch and the
    // armed re-open all arrive here — while the band claim carries only the
    // click. The hover routes are arguably out of reach with an edit open (the
    // row arms only through a click, which would itself have closed the edit),
    // but a rule that is impossible by construction is worth more here than one
    // that is unreachable by argument. A toggle that CLOSES a menu discards
    // nothing: it returned above.
    //
    // The teardown is called DIRECTLY rather than through
    // close_top_flag_editor_for_outside_press, whose job includes the
    // is-the-press-inside-the-box test. There is no press here at all on the
    // hover routes, and the rect test is not merely skipped but INAPPLICABLE: a
    // row-1 button cannot be inside the flag editor's box, which lives in the
    // marker lane below the whole top strip's button rows.
    //
    // THE REST OF ROW 1 IS DELIBERATELY OUT OF SCOPE. Quit needs nothing: its
    // Ctrl+Q is one of the three chords the keyboard-modal gate admits, so it
    // tears the edit down through its own route. The view bar's bare 1/2/3 drop
    // at that gate as consumed nothings — the modality ruling working as
    // intended — and ending an edit there would be a behavior change nobody
    // asked for.
    if (text_editor::is_active(app.top_flag_editor)) {
        flag_editor.exit_top_flag_edit_no_commit();
    }
    app.dropdown.menu         = menu;
    app.dropdown.hovered_item = -1;
    // OPENING A MENU ARMS THE ROW — the mode's ONE producer, and it sits here
    // because this is the ONE route that opens either menu: the anchor click, the
    // hover switch, and the armed hover re-open all arrive through it, and no
    // keyboard chord opens a dropdown at all. So "a menu is open" implies "the
    // row is armed" by construction, with no second producer to keep in step.
    // The bit's contract — what the mode does and what ends it — is at the field
    // (AppState::Dropdown::menu_row_armed).
    app.dropdown.menu_row_armed = true;
    // THE OPEN EDGE DAMAGES THE BOX BEFORE THE BOX EXISTS. Its rect is not
    // published until paint_dropdown runs, and a redraw is CLIPPED to the
    // damage it was handed — so strip damage alone would clip away whatever the
    // popup hangs past the strip. At 100% the settings menu happens to fit
    // (175px of popup inside a 217px strip); at 200% it overhangs by ~40px and
    // the bottom band would never paint.
    //
    // The HEIGHT is derivable without painting (dropdown_h_px — item count
    // times item height, plus the separator blocks and the borders), so the
    // damage is exact vertically. The WIDTH is not (it needs the widest shaped
    // label), so this damages FULL WIDTH from the button's top down — a band,
    // not a guess, and cheap because it happens once per open.
    //
    // THE TOP EDGE IS THE BUTTON'S BOTTOM, the same expression paint_dropdown
    // places the box at — the dropdown hangs off the thing that opened it
    // (architect 2026-08-02), which since row 1 gained its 1px margin-bottom is
    // one pixel above the lane's own bottom. Both sites read the SAME stashed
    // rect through the SAME anchor owner, so the damaged band and the painted
    // box start on the same row of pixels; a band anchored a pixel lower would
    // leave the popup's own top border unpainted.
    {
        const GuiRect& btn =
            app.redesign_buttons[redesign_button_index(
                dropdown_anchor_button(menu))].rect;
        viewport.invalidate_rect(
            GuiRect{0, btn.y + btn.h, app.width, dropdown_h_px(menu)});
    }
    // THE TOOLTIP GOES DOWN ON THE OPEN EDGE — the two floating surfaces cannot
    // coexist (paint_handler.h states the pair), and this is the one line that
    // makes that structural rather than a reachability argument about which
    // routes reach an open. It is NOT the roster clear's doing:
    // clear_redesign_button_hover writes the faces' `hovered` bits and nothing
    // else, the tooltip's dwell and visibility living in their own state
    // (AppState::redesign_tooltip) with hide_shift_tooltip as their one hide
    // owner. The hover recompute would reach the same answer on the NEXT motion
    // (no roster button hovers under a popup, so the dwell walk finds no owner
    // and hides), but "next motion" is not a property — an open reached with the
    // pointer standing still has no next motion.
    hide_shift_tooltip();
    // THE ROSTER UNHOVERS AT THE OPEN: the pointer belongs to the popup, and
    // redesign_button_hoverable refuses the WHOLE roster while a dropdown is up.
    // No motion event follows a press, so a face lit at the moment of the open
    // would otherwise stay lit under a surface that has taken the pointer from
    // it. It is also what keeps the tooltip DOWN for as long as the popup is up:
    // with no button hoverable, the dwell writer (recompute_redesign_button_
    // hover) can never stamp a new one.
    //
    // ROW 1 IS CLEARED HERE TOO AND STAYS CLEAR while the menu is up; it
    // re-resolves at the next motion or the next tick (main.cpp runs the same
    // recompute per frame, gesture-gated only) once the popup is down. That
    // costs nothing visible: the only row-1 button the pointer can be on at an
    // open edge is the one just pressed, and that is the popup's ANCHOR, whose
    // pill the paint condition below keeps regardless of the hover bit.
    //
    // IT NO LONGER DECIDES THE MENU BUTTON'S PILL, and the inversion is worth
    // stating because this line used to be what darkened it: since 2026-08-02
    // the pill paints on the popup's own ANCHOR as well as on hover (kdenlive's
    // behaviour, argued at the paint site), so the button whose menu is up stays
    // blue for exactly as long as it is up. This clear now serves the OTHER
    // roster buttons and the tooltip; the anchor's face is the paint condition's
    // business.
    clear_redesign_button_hover();
    viewport.invalidate_top_strip();
}

// THE MENU ROW'S MODE, EXIT HALF — "the pointer left row 1, go cold", which is
// what keeps the mode from outliving the visit: wander down to the waveform and
// Settings needs a click again. The band is top_menu_row_area, the press claim's
// own rect, so "on the row" means one thing to both.
//
// IT RUNS WHEREVER MOTION IS SEEN — on_motion's very top, above every branch —
// and that is the half's whole design. The OPEN half below has a guard list (it
// must not spring a menu open under a modal or mid-gesture); this half has none,
// because a modal owning the pointer is a reason not to OPEN a menu and no
// reason at all to forget that the pointer left the row. Sitting in the
// no-gesture tail with the open half is exactly what it must not do: a prompt,
// an editor, an editor text drag or any live gesture returns above that tail, so
// the pointer could leave row 1 unnoticed and carry an invisible armed mode out
// of the visit.
// RUNNING IT DURING A GESTURE COSTS NOTHING AND IS NOT A CHANGE OF RULE: a press
// already ended the mode (on_button_press's top), so a live gesture's motion
// finds the bit false and this is a compare. What it buys is the modal branches.
// WITH A MENU OPEN IT MUST NOT FIRE — the popup hangs below the row, so the
// pointer leaves the band the moment it moves into it — and it cannot:
// disarm_menu_row carries that gate for all of its callers.
void GuiInputHandler::update_menu_row_exit(int mouse_x, int mouse_y) {
    if (!app.dropdown.menu_row_armed) return;
    if (rect_contains(top_menu_row_area(app), mouse_x, mouse_y)) return;
    disarm_menu_row();
}

// THE MENU ROW'S MODE, OPEN HALF (architect 2026-08-03): once a menu has been
// opened from row 1, the two anchors open on the POINTER ALONE — the menu-bar
// behaviour every desktop has, and the completion of the row-1 hover close,
// which puts a menu away and now leaves the row able to bring one back. The bit
// and its whole contract are at AppState::Dropdown::menu_row_armed.
//
// ITS PLACEMENT IS ITS GUARD LIST. It is called from on_motion's no-gesture
// tail and nowhere else, so the conditions the re-open must not fire under are
// the branches that already return above it — an open dropdown (which owns the
// motion outright), the prompt, the editor text drag, the two bottom-strip
// keyboard-modal editors, and every live gesture and pending. No condition is
// restated here, and none is worth adding: that tail is exactly the reachability
// the anchors' own PRESS claim has, so the hover opens a menu in precisely the
// states in which a click opens one. (The pointer-transparent FLAG editor gates
// neither route, by its own ruling — see the press claim; the open it leads to
// ENDS that edit, which is toggle_dropdown's business and not restated here.)
// IT TESTS NO BAND. Every anchor rect lies inside row 1 by construction, so a
// hit IS "on the row"; the exit half above owns the band question, at the one
// placement that can answer it for every branch.
void GuiInputHandler::open_menu_row_anchor_on_hover(int mouse_x, int mouse_y) {
    if (!app.dropdown.menu_row_armed) return;
    // ON AN ANCHOR, OPEN ITS MENU — through toggle_dropdown, the same owner the
    // CLICK uses, so the anchor expression, the open edge's damage and the roster
    // clear are one route with nothing restated. The walk covers every menu that
    // HAS an anchor rather than naming the pair, so dropdown_anchor_button stays
    // the one place that knows which button emits which menu. Anywhere ELSE on
    // the row — Quit, the view bar, the ground between them — is simply not an
    // anchor: the row stays armed and nothing opens.
    for (const DropdownMenu m : {DropdownMenu::Settings,
                                 DropdownMenu::Navigation}) {
        if (!redesign_button_hit(app, dropdown_anchor_button(m),
                                 mouse_x, mouse_y)) continue;
        toggle_dropdown(m);
        break;
    }
}

// THE MODE'S END, the one gated writer every route that is not close_dropdown
// goes through. Its callers, re-derived by grep: the band exit above, the
// pointer-leave hook (main.cpp, beside the row's other face clears — no motion
// event follows that edge), ANY pointer press (on_button_press's top) and ANY
// key press (on_key's top). It damages nothing: the mode is invisible, painting
// no face of its own; what it changes is what the NEXT motion does.
//
// THE "NO MENU OPEN" GATE IS THIS FUNCTION'S REASON TO EXIST rather than four
// inline writes. Leaving the window is NOT a dismissal — the popup stays up, as
// clear_dropdown_press beside it states — and a menu still standing is still the
// mode, so re-entering over the other anchor must SWITCH rather than find a cold
// row (and the row-1 hover close, which re-arms, must not be resurrecting a mode
// something else meant to end). The same answer serves the two press callers for
// a second reason: while a popup is up, a press or a chord belongs to the POPUP,
// whose own routes (close_dropdown, the toggle) decide the mode — so these
// blanket disarms cannot get in front of them. A dismissal with a menu open
// therefore always ends the mode through close_dropdown, and a dismissal with
// none open ends it here.
void GuiInputHandler::disarm_menu_row() {
    if (app.dropdown.open()) return;
    app.dropdown.menu_row_armed = false;
}

// THE ARMED ITEM'S DROP, with no release to follow (the pointer-leave edge): no
// release will arrive, so the face would otherwise stay lit under a pointer that
// is gone. The menu itself STAYS OPEN — leaving the window is not a dismissal.
void GuiInputHandler::clear_dropdown_press() {
    if (app.dropdown.pressed_item < 0) return;
    app.dropdown.pressed_item = -1;
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(app.dropdown.rect);
}

// THE HOVER TOOLTIP'S HIDE. Its callers, re-derived by grep: the hover
// recompute (the hover ended, or moved to another tooltip-bearing button), ANY
// pointer press and ANY key press (the hint's job ends the moment the user
// acts), any wheel, the dropdown's OPEN edge (the two floating surfaces never
// coexist), and TWO main.cpp hooks — the pointer-leave / capability-loss edge
// and the compositor close, the one modal opener that arrives with no key press
// to carry the key-press hide's own rule.
// THAT LAST ONE IS WHY THE RECOMPUTE IS NOT ENOUGH BY ITSELF. It damages the
// strip AND the box's last painted rect, and the box hangs BELOW the strip — so
// an edge that clears the hover bits without hiding here leaves the paint with
// no owner to draw, which publishes a zero rect and returns, and the overhang
// below the strip then has nothing left to erase it. clear_redesign_button_hover
// has exactly TWO callers, re-derived by grep, and both are covered: the
// dropdown's open edge hides two lines above its clear, and the leave hook now
// hides beside its own.
// Showing is the tick's job (the dwell); this is only the hide, plus the stamp
// reset that makes the next hover start its dwell from zero.
void GuiInputHandler::hide_shift_tooltip() {
    app.redesign_tooltip.hover_ms = 0;
    app.redesign_tooltip.owner    = -1;
    if (!app.redesign_tooltip.visible) return;
    const GuiRect painted = app.redesign_tooltip.rect;
    app.redesign_tooltip.visible = false;
    viewport.invalidate_top_strip();
    viewport.invalidate_rect(painted);
}

// THE CLICK FACE, dropped. The face rides the PHYSICAL button hold, so the
// two edges that end a hold both come here: the left release (on_button_release,
// at its very top so a modal or an early return cannot strand a lit button) and
// the pointer-leave / capability-loss hook, which is the button-lost edge — no
// release event follows a leave, so without this a pointer that slides out of
// the window mid-press would leave the interior filled forever. Transition-gated
// like the hover clear beside it, and one invalidate_top_strip when it fires.
void GuiInputHandler::clear_redesign_button_press() {
    if (app.redesign_pressed < 0) return;
    app.redesign_pressed = -1;
    viewport.invalidate_top_strip();
}

// Motion handler. Drives the active pointer gesture: editor-text drag,
// strip-row zoom/pan drag, trim drag (or
// a pending trim drag arming past the threshold), region-select drag, or
// marker reposition drag (or a pending
// marker drag); with no gesture it recomputes hover at the cursor. The
// marker drag applies the pointer delta to the grabbed marker; the playhead
// follows the grabbed marker unconditionally (apply_drag_motion owns that —
// the arming plain click already landed the playhead on the marker, so the
// drag tows it by construction).
void GuiInputHandler::on_motion(int mouse_x, int mouse_y, GuiInputState mods) {
    // Record latest cursor coords so viewport mutators can re-evaluate hover
    // at the cursor's last position.
    app.last_mouse_x = mouse_x;
    app.last_mouse_y = mouse_y;
    app.pointer_in_window = true;
    // THE MENU ROW'S MODE ENDS WHEN THE POINTER LEAVES ROW 1, and that half is
    // resolved HERE, above every branch, because it is the only placement that
    // sees every motion: the modal branches and every live gesture return before
    // the no-gesture tail where the mode's OPEN half lives, and a mode that could
    // not notice the pointer leaving under a prompt or an editor would be carried
    // invisibly out of the visit. A modal owning the pointer is a reason not to
    // OPEN a menu, not a reason to forget where the pointer went. The two halves
    // and their asymmetric guard lists are at their definitions
    // (update_menu_row_exit / open_menu_row_anchor_on_hover).
    update_menu_row_exit(mouse_x, mouse_y);
    // THE POINTER CURSOR, resolved ONCE and ABOVE every gesture branch —
    // including the modal ones, which all return before the no-gesture tail.
    // Placed here so that every early return below still leaves the pointer
    // showing the right cursor for where it actually is; a call further down
    // would freeze the cursor under exactly the states (a live drag, an open
    // menu) whose refusals the zone map already spells. The platform applies
    // only on a CHANGE, so the common case — an unmoving zone under a moving
    // pointer — costs one map lookup and no protocol traffic.
    gui.set_cursor_kind(pointer_cursor_kind(mouse_x, mouse_y, mods));
    // THE BUTTON HOVER IS A POINTER FACT AND FOLLOWS THE POINTER, under every
    // modal surface (architect 2026-07-31, fixing a stale Settings pill). It is
    // recomputed in the two MODAL branches that return before the no-gesture
    // tail, here and at the bottom-strip editors below, because a modal freezing
    // it leaves a lit pill under a pointer that has moved away — visible as a
    // button still coloured after Esc/Enter closes the editor, clearing only at
    // the next motion. THE ARGUMENT IS THAT HOVER IS A SEPARATELY MAINTAINED
    // POINTER FACT — "the pointer is over this rect" is true or false regardless
    // of who owns the keyboard — and NOT that these surfaces let pointer input
    // through: only the top-strip FlagPayload editor is pointer- and
    // wheel-transparent, while the settings editor, the render-commit editor and
    // the BpmBracket kind (modal_bottom_strip_editor_active, which is also the
    // wheel swallow) take every press outside their own text row and drop it. The
    // prompt takes the same answer for the same reason: it suppresses no other
    // pointer affordance either (the marker hover POPUP's suppression right here
    // is the one ruled exception, and it is a different fact: a resolved marker
    // readout, not "the pointer is over this rect"). What the recompute must NOT
    // do under those surfaces is start a TOOLTIP dwell — a floating hint is not a
    // face, and that refusal lives in the recompute itself.
    // What DOES still freeze the hover is an active pointer GESTURE — the
    // branches below all return without this call, exactly as before.
    //
    // NO CLOSE-EDGE HOOK EXISTS OR IS NEEDED: with the recompute live through
    // the whole modal, the stash is already correct when the editor closes. A
    // pointer resting ON the button at the close keeps its lit pill, and that is
    // hover, not staleness.
    //
    // AN OPEN DROPDOWN REFUSES THE WHOLE ROSTER (redesign_button_hoverable,
    // app_state.h), so its branch below recomputes for one reason only: the frame
    // on which a row-1 button CLOSES the menu must also light that button, and
    // running the recompute after the close is what makes it one frame rather than
    // two. While the menu stays up the walk re-derives all-false, which costs a
    // pass of rect compares and no damage.
    // The ANCHOR of the open menu lights through the paint condition
    // (paint_menu_row) rather than through the hover bit — that is what keeps its
    // pill through the open edge's clear and what makes a switch visible on the
    // frame it happens.
    //
    // THE OPEN DROPDOWN TAKES THE MOTION, above every gate: it owns the pointer,
    // so no gesture can be live under it (its own press consumed everything).
    if (app.dropdown.open()) {
        // A NON-MENU ROW-1 BUTTON CLOSES THE OPEN MENU (architect 2026-08-03,
        // from kdenlive: only ONE button in that row is lit at a time, so sliding
        // from Navigation onto Quit must put Navigation's menu away rather than
        // leave it hanging under a second lit button). This REVERSES the earlier
        // "a menu bar keeps its menu up while the pointer crosses the rest of the
        // bar" reading — the bar's other buttons are not menu titles here, they
        // are commands, and a command button lighting beside an open menu is the
        // state the row does not have.
        //
        // THE MEMBERSHIP IS redesign_button_in_menu_row (app_state.h), walked
        // rather than named, so Quit and the view bar's three are covered by the
        // fact "row 1" and a new row-1 button inherits the rule by existing. An
        // ANCHOR is skipped: the OPEN menu's own does nothing at all (no re-open,
        // no close) and the OTHER one SWITCHES through the walk below, both
        // unchanged. The close goes through close_dropdown, the one close owner,
        // which carries the popup's damage.
        //
        // IT RUNS BEFORE THE ROSTER RECOMPUTE so the frame that closes the menu is
        // the frame the button lights on: with the popup already gone,
        // redesign_button_hoverable's dropdown refusal no longer applies and the
        // button under the pointer resolves to hovered in the very same call.
        for (int i = 0; i < kRedesignButtonCount; ++i) {
            const RedesignButton id = static_cast<RedesignButton>(i);
            if (!redesign_button_in_menu_row(id)) continue;
            if (id == dropdown_anchor_button(DropdownMenu::Settings) ||
                id == dropdown_anchor_button(DropdownMenu::Navigation)) continue;
            if (!redesign_button_hit(app, id, mouse_x, mouse_y)) continue;
            close_dropdown();
            // THE MODE SURVIVES THIS ONE CLOSE (architect 2026-08-03, the other
            // half of the same behaviour): sliding onto Quit puts the menu away
            // but leaves the row ARMED, so sliding BACK onto Settings or
            // Navigation opens that menu again with no click. This is a step
            // across the bar, not a dismissal, and close_dropdown disarms by
            // default — so the exception is spelled here, at the only site that
            // needs it.
            app.dropdown.menu_row_armed = true;
            break;
        }
        // The roster's own faces. While a popup is up this re-derives false for
        // the WHOLE roster (redesign_button_hoverable refuses every button then —
        // the pointer belongs to the popup), and after the close above it resolves
        // the row-1 button the pointer landed on normally, which is the one case
        // that needs it here.
        recompute_redesign_button_hover();
        // HOVERING THE OTHER MENU'S BUTTON SWITCHES TO IT (architect
        // 2026-08-03) — the menu bar's standing behaviour: open one menu, slide
        // onto the next button without pressing, and that button's menu is what
        // is up. The switch goes through toggle_dropdown, the same owner the
        // CLICK uses, so the close-then-open, the anchor expression and the open
        // edge's damage are one route with nothing restated here; a menu that is
        // not the open one makes that toggle's same-menu test false, which is
        // exactly the switch. The button it switches ONTO lights through the
        // painter's own anchor condition (paint_menu_row), not through the hover
        // bit, which is why the order of these two blocks is set by the CLOSE rule
        // above and not by the switch.
        //
        // The walk covers every menu that HAS an anchor instead of naming the
        // pair, so the anchor owner stays the one place that knows which button
        // emits which menu. Hovering the OPEN menu's own anchor is skipped
        // outright — no re-open, no close, no damage. A row-1 button owning no
        // dropdown never reaches here at all: the close rule above consumed it
        // and the menu is already down.
        for (const DropdownMenu m : {DropdownMenu::Settings,
                                     DropdownMenu::Navigation}) {
            if (m == app.dropdown.menu) continue;
            if (!redesign_button_hit(app, dropdown_anchor_button(m),
                                     mouse_x, mouse_y)) continue;
            toggle_dropdown(m);
            break;
        }
        // The popup's own item hover, beside row 1's — the two are the whole
        // hover answer while a menu is up, and they cannot collide, since the
        // box starts below the row. AFTER
        // A SWITCH the new menu's item rects have not been published yet (the
        // painter publishes them), so this resolves to no item on this frame and
        // the row under the pointer lights on the next — correct as it stands,
        // and the reason nothing here reaches into the painter for geometry.
        recompute_dropdown_hover();
        return;
    }
    if (app.prompt.active) {
        recompute_redesign_button_hover();
        return;
    }
    // F2.1: editor-text drag motion. Handled before the settings swallow
    // (which returns) so the gesture reaches the bottom-strip editors, and
    // before the trim / playhead branches. A lost button finalizes like
    // release, mirroring those handlers.
    if (app.editor_text_drag.active) {
        if (!mods.primary_button_held) {
            finalize_editor_text_drag();
            return;
        }
        const ActiveEditorText g = active_editor_text(app, audio);
        if (g.valid) {
            // The anchor set at press stays put; moving cursor_pos extends
            // the selection.
            set_editor_caret_from_x(g, mouse_x);
            if (g.bottom_strip) viewport.invalidate_timestamp_area();
            else                viewport.invalidate_top_strip();
        }
        // !g.valid (only an invalid editor target — the lane text stays
        // onscreen even off-view): no-op this frame, leaving the caret put.
        return;
    }
    if (text_editor::is_active(app.settings_editor) ||
        text_editor::is_active(app.commit_editor)) {
        // The button hover stays live under a keyboard-modal editor — the
        // rationale is at the prompt branch above, and THIS is the branch the
        // reported staleness came through (the Settings button opens the
        // settings editor, whose gate is right here).
        recompute_redesign_button_hover();
        return;
    }
    // Dual-axis strip drag (the incremental v6 model; see apply_strip_drag_at).
    // Each event pans by its dx at the live level and zooms by its dy off the
    // live level, pivoting the zoom around the (edge-rebindable) song anchor. The
    // repaint is SYNCHRONOUS (final_event=false): one full rebuild whenever the
    // level or the viewport moved, a true
    // no-op when neither did — affordable because the platform coalesces captured
    // motion to one event per pointer frame. The release runs the one synchronous
    // rebuild plus the predictor resync. A lost button finalizes like release.
    if (app.strip_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (app.strip_drag.moved) {
                apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/true);
            }
            // A motionless press ends with NO click action (the ctrl-waveform
            // selection clear is RETIRED — a motionless ctrl+waveform
            // press-release commits nothing on the clean release either, so this
            // abnormal end matches it for free).
            app.strip_drag = StripDragState{};
            // An abnormal termination (button lost, not a clean release) seeds
            // no double-click candidate and drops any pending one.
            app.double_click = DoubleClickCandidate{};
            end_strip_pointer_capture();
            return;
        }
        // Sub-pixel capture jitter must not promote a click to a drag: while the
        // press has not yet become a drag, apply nothing until the pointer has
        // travelled at least the Chebyshev threshold from the press, leaving the
        // drag armed but unmoved. This gate decides only WHETHER the press
        // becomes a drag — once moved it never re-engages, so dragging back near
        // the press mid-drag has no dead zone. last_x/last_y stay at the press
        // until the crossing, so the crossing event folds the whole accumulated
        // delta since the press and no travel is lost.
        if (!app.strip_drag.moved &&
            std::max(std::abs(mouse_x - app.strip_drag.press_x),
                     std::abs(mouse_y - app.strip_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
        app.strip_drag.moved = true;
        apply_strip_drag_at(mouse_x, mouse_y, /*final_event=*/false);
        return;
    }
    // Alt+drag grab-pan (continuous 1:1). The viewport snaps to whole pixels in
    // clamp_viewport_start (reached through scroll_viewport), so a per-event pan
    // re-anchored by that snap tracks the cursor 1:1 without drift — no carried
    // sample remainder. scroll_viewport renders the plate synchronously, so
    // per-event work is one full-width render — the cost zoom already paid per
    // pointer frame, and the reason a panning plate looks identical to a resting
    // one (architect 2026-07-26). A
    // lost button ends it like release (re-anchor the predictor once). The
    // wheel keeps its quantized detent step; only the drag is continuous.
    if (app.scroll_drag.active) {
        if (!mods.primary_button_held) {     // button lost -> end like release
            if (playback.is_playing()) playback.resync_predictor();
            app.scroll_drag = ScrollDragState{};
            end_strip_pointer_capture();     // reappear the cursor (idempotent)
            return;
        }
        const double spp = current_samples_per_pixel(app, audio);
        const int    dx  = mouse_x - app.scroll_drag.last_x;
        app.scroll_drag.last_x = mouse_x;
        const int64_t delta =
            static_cast<int64_t>(std::nearbyint(static_cast<double>(dx) * spp));
        if (delta != 0) {
            // Grab-pan: drag right (dx>0) reveals earlier content, viewport
            // moves left. The mid-gesture guarantee this gesture needed — never
            // painting over a plate from an older basis, a staleness mechanism
            // the async deferral was once convicted of — now comes free: every
            // scroll renders synchronously and drains a busy worker, so it no
            // longer has to ask for a distinct pan mode.
            viewport.scroll_viewport(-delta, /*continuous=*/true);
        }
        return;
    }
    // (No scrub motion branch: the scrub is a ONE-SHOT act at the press — the
    // held-drag per-column re-scrub is REMOVED (architect
    // 2026-07-23, the Ableton model), so motion over the scrub surfaces is
    // inert and the per-column stop-fence cadence is structurally gone.)
    // Trim-boundary drag motion. Handled before the marker-drag branch;
    // active in BOTH views (begin_trim_drag has no view gate, and
    // update_trim_drag / commit_trim_drag carry the target-view cached-map
    // machinery). A lost button commits at the current position, mirroring the
    // marker-drag motion handler.
    if (app.trim_drag.active) {
        if (!mods.primary_button_held) {
            commit_trim_drag();
            return;
        }
        update_trim_drag(mouse_x);
        return;
    }
    // Pending trim drag (armed by a plain trim-bar press): the trim reposition
    // begins only once the pointer travels past the shared Chebyshev threshold.
    // A lost button before the crossing ends it as a motionless click (nothing
    // committed). Placed after the trim_drag branch above: on the crossing this
    // begins the drag AND applies its first update inline, so it does not fall
    // back into that branch this event.
    if (app.pending_trim_drag.active) {
        if (!mods.primary_button_held) {   // button lost -> just the click
            // The motionless endcap/bridge press commits NOTHING (architect
            // 2026-07-30, the clean-release twin above): its highlight publish is
            // retired, so the same physical click rests identically whichever
            // path ended it — as a consumed nothing.
            app.pending_trim_drag = PendingTrimDrag{};
            // THE TRIM-BAR SEED DIES WITH IT. A button lost mid-press is not a
            // clean click sequence, so it may not leave a seed behind for an
            // unrelated later release to consume into a TrimBar double-click
            // candidate — the same rule the force-end finalizer follows, and the
            // lifetime the seed's own declaration states (app_state.h). Only the
            // CLEAN release is allowed to seed.
            app.trim_bar_press = TrimBarPressSeed{};
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_trim_drag.press_x),
                     std::abs(mouse_y - app.pending_trim_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // Threshold crossed: begin the trim drag anchored at the PRESS column so
        // the bound(s) track from the grab, this first update folding the whole
        // press->crossing delta (the marker-pending / strip catch-up pattern).
        // begin_trim_drag captures the anchor at press_x now — exact, since
        // nothing mutated the trim store between press and crossing — and sets
        // app.trim_drag.active.
        // THREE FIELDS CROSS, and that is the whole transfer (2026-07-29): the
        // pending's set_click / pre-press pair / selection+region snapshots were an
        // Esc-restore origin, and pointer gestures have no cancel (the rule at the
        // drag-modal gate in input_handler.cpp), so the cross-struct data path they
        // needed is gone. A bound-set-armed drag is now indistinguishable from a
        // plain one here — correctly: its click-set already committed, and
        // begin_trim_drag's orig_* capture (the click-set values) is exactly the
        // basis the drag mechanics want, both for the rigid pair delta and for
        // commit's moved-bound test.
        const bool is_begin = app.pending_trim_drag.is_begin;
        const bool both     = app.pending_trim_drag.both;
        const int  press_x  = app.pending_trim_drag.press_x;
        app.pending_trim_drag = PendingTrimDrag{};
        begin_trim_drag(is_begin ? TrimHit::Begin : TrimHit::End, press_x, both);
        if (!app.trim_drag.active) return;  // begin refused (no pair / no audio)
        update_trim_drag(mouse_x);
        return;
    }
    // Motion just continues whatever the press already armed — the
    // home-view gate (active_column_authoring_allowed, the whole gate now that the
    // tempo drag and its eligibility check are gone) ran once at arm time in
    // on_button_press, so nothing here re-checks view or column; the
    // region drag below is navigation, not authoring, and was never
    // gated. Per-site translation (drag anchor capture, motion delta
    // conversion, hit tests) lives in the handlers below.
    if (app.region_drag.active) {
        // Left button must still be held; if not, the release was lost —
        // end the gesture, resting the region at its current extent (as a
        // clean release would). Modifier changes mid-drag are ignored. A
        // sub-threshold sliver rest dissolves like a click, exactly as the
        // clean release branch does (end_region_drag_min_size_check) — and
        // identically gated on `moved`: a
        // MOTIONLESS lost button on the shift-exact preserving arm must not delete
        // the legal narrow region it left resting. Capture `moved` before the
        // reset zeroes it; plain path unaffected for the same reasons as the clean
        // release branch.
        if (!mods.primary_button_held) {
            const bool moved = app.region_drag.moved;
            app.region_drag = RegionDragState{};
            if (moved)
                end_region_drag_min_size_check(app, audio, viewport);
            return;
        }
        const GuiRect area = waveform_area(app);
        if (area.w <= 0) return;
        // Sub-threshold: the press has not yet become a drag. Below the shared
        // Chebyshev gate nothing extra happens — the press already did the
        // click and cleared any resting region at mouse-down. Once a drag,
        // always a drag (moved never re-engages). `crossing` = this event is the
        // transition to moved (captured before we set the flag); it force-installs
        // the anchored span below, past the same-column short-circuit.
        const bool crossing = !app.region_drag.moved;
        if (crossing &&
            std::max(std::abs(mouse_x - app.region_drag.press_x),
                     std::abs(mouse_y - app.region_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;
        }
        app.region_drag.moved = true;
        // A moved region drag drops any double-click candidate: this press became
        // a drag, not the first click of a double-click. Only the empty
        // flag/triangle-lane press seeds a candidate before arming this drag (a
        // waveform press seeds none), so this keeps a lane drag from later
        // consuming as an EmptyLane marker-create double-click — the standing
        // moved-drag clear route.
        app.double_click = DoubleClickCandidate{};
        // Far endpoint at the pointer column, through the same click->frame
        // basis as the anchor, clamped to the visible strip like the other
        // drags' live tracking. Endpoints are active-domain frames, so the
        // span survives pan/zoom mid-drag and at rest. Also clamped into the
        // live domain: at a fractional flush-right zoom the painter-quantized
        // wall differs from the click conversion, so the last visible column's
        // frame can land at domain_total — one past [0, domain_total-1] — which
        // the display-state validator would clear wholesale (same rule as the
        // press-site anchor).
        int rel = mouse_x - area.x;
        if (rel < 0) rel = 0;
        if (rel >= area.w) rel = area.w - 1;
        const int64_t far_frame = clamp_playhead_to_live_domain(
            playhead_frame_at_click_column(app, audio, rel), app, audio);
        // Column-change gate: the span changes only when the far endpoint moves
        // to a new frame. A same-frame motion event (sub-pixel jitter within one
        // column) is a no-op — skip the repaint. The anchor is fixed for the
        // gesture, so the far endpoint alone decides the span. The CROSSING event
        // ALWAYS installs {anchor, pointer}, bypassing this short-circuit: the
        // plain arm cleared the region
        // (active == false) so it would proceed anyway, but the SHIFT-exact
        // preserving arm leaves the OLD region active, and its stale b_frame can
        // coincide with the first crossed column's frame — without the bypass the
        // crossing marks moved yet returns before installing, resting the stale
        // span indefinitely. The short-circuit's purpose (redundant-damage
        // avoidance on same-column motion) survives for every LATER event.
        if (!crossing && app.region.active && far_frame == app.region.b_frame)
            return;
        app.region.active     = true;
        app.region.a_frame    = app.region_drag.anchor_frame;
        app.region.b_frame    = far_frame;
        // SELECTION FLOWS DOWNWARD ONLY (architect 2026-07-23): highlighting a
        // region does NOT select the markers it contains (the reverse coupling —
        // a region selecting its contents — was tried and retired; do not
        // re-propose) — the press already deselected all and the drag
        // leaves the selection EMPTY throughout. That is now the WHOLE story: the
        // region is trim scratch with exactly two formers, this drag and the
        // shift waveform press, and both deselect at press — so a region rests
        // ONLY beside an EMPTY selection, structurally (the inventory is at
        // RegionState, app_state.h).
        //
        // THE DRAG CARRIES THE PLAYHEAD (architect 2026-07-30, live-test
        // refinement: "i'd prefer the playhead move along with the drag for
        // region highlight - more intuitive"). The cursor rides the MOVING
        // endpoint — far_frame, already clamped playable by the conversion above,
        // so the write needs no clamp of its own — while the anchor stays put as
        // the span's other bound. BOTH ARMS ride this one motion path, and the
        // press-time asymmetry between them is unchanged: the plain upper-half
        // press placed the playhead at its click column, and the SHIFT-exact
        // former's press still moves NO playhead at all (its ruled shape — it
        // forms a span and drops the selection, nothing else), but once either
        // press becomes a drag the pointer carries the cursor from here on.
        // DIRECT CURSOR WRITE, not move_playhead_to: a keep-visible edge-align
        // would scroll the viewport out from under a live gesture, and the span's
        // endpoints are painted against the viewport the drag started in.
        // PLAYBACK IS UNTOUCHED per motion: the plain press's at-press
        // reseek-keeping-alive stands as the whole playback story of this
        // gesture, and a per-column reseek would re-cue the audio on every pixel.
        // The column-change short-circuit above keeps this to ONE write per
        // CHANGED column. The waveform invalidate below repaints the cursor's
        // HEAD AND STEM with the ground — its rect runs from the window top
        // down through the waveform, so the marker-lane head is inside it (the
        // triangle this used to name retired with its lane in row 5); the
        // TIMESTAMP invalidate is owed
        // separately because the bottom-strip readout shows this cursor whenever
        // no scanner is active, and it lives outside the waveform area.
        // A SLIVER RELEASE LEAVES THE PLAYHEAD WHERE THE DRAG PUT IT: the
        // release-time min-size check dissolves the span but writes no playhead,
        // which is the same "what stands stands" rule every pointer gesture has
        // (there is no cancel) — accepted.
        app.playhead_cursor_sample = far_frame;
        viewport.invalidate_waveform_area();
        viewport.invalidate_timestamp_area();
        return;
    }
    // (No tempo-drag motion arms: the target-view tempo drag and its pending are
    // DELETED, architect 2026-07-29 — see marker_drag.h. W+target has no pointer
    // authoring gesture at all now.)
    // Pending marker drag (armed by a plain flag press): usually the marker was
    // single-selected at press; the reposition begins only once the pointer
    // travels past the shared Chebyshev threshold (kDragMovedThresholdPx, now
    // one generic 8px gate for every press-becomes-drag surface). Handled before the hover
    // fallthrough below and after the other drag branches (a pending drag and
    // any other pointer gesture are mutually exclusive — the arming press does
    // no other work). A lost button before the crossing ends it as a plain
    // click.
    if (app.pending_marker_drag.active) {
        if (!mods.primary_button_held) {   // button lost -> just the click
            // A lost button before the crossing IS the click, matching the release
            // path: the press already committed the whole click (single-select,
            // land, span collapse), so this just disarms (the deferred completion
            // died with the group drag — see the release branch).
            app.pending_marker_drag = PendingMarkerDrag{};
            return;
        }
        if (std::max(std::abs(mouse_x - app.pending_marker_drag.press_x),
                     std::abs(mouse_y - app.pending_marker_drag.press_y)) <
                kDragMovedThresholdPx) {
            return;   // still a click; leave the pending armed, do nothing
        }
        // Threshold crossed: begin the drag anchored at the PRESS column so the
        // marker tracks the pointer 1:1, this first apply folding the whole
        // press->crossing delta (the strip/region catch-up pattern). begin_drag
        // captures the pre-drag snapshot / selection / walls now — exact, since
        // nothing mutated the store between press and crossing — and sets
        // app.drag.active. Fall through (no return) so this same motion event
        // applies the first delta through the marker-drag branch below.
        const int marker  = app.pending_marker_drag.marker;
        const int press_x = app.pending_marker_drag.press_x;
        app.pending_marker_drag = PendingMarkerDrag{};
        // A moved marker drag drops any double-click candidate: this press is a
        // drag, not a click of a marker double-click. The arming press seeded a
        // Marker candidate (press-time seeding), so this clear is what keeps a
        // moved drag from carrying one — the load-bearing half of seeding at
        // the press.
        app.double_click = DoubleClickCandidate{};
        if (!marker_drag.begin_drag(marker, press_x)) {
                return;   // begin refused (bad index / no audio): drop the gesture
        }
        // No follow override needed: the marker drag always begins from a
        // top-strip flag press, which already stopped playback (the marker
        // click owns that stop), so there is no live playhead to chase.
    }
    if (!app.drag.active) {
        // The redesigned rows' own hover, resolved in the same no-gesture tail
        // and through its own state (a button is not a marker, so it has no
        // place in the hover-popup machinery below). An ACTIVE GESTURE FREEZES
        // IT — the branches above all returned — which is consistent and
        // harmless: a gesture that ends over a button re-resolves on the next
        // motion, and a press cannot reach one mid-gesture anyway.
        // The redesigned rows' hover is the ONLY hover left: the marker hover
        // popup and its whole recompute machinery died with the marker-text lane
        // (row 5), so the no-gesture tail has nothing else to resolve.
        //
        // THE MENU ROW'S MODE, OPEN HALF, resolves here beside the faces and for
        // the same reason: opening a menu at a pointer is a thing only a
        // gesture-free, modal-free pointer may do, and every branch that must
        // forbid it has returned above. With the row ARMED, this opens an
        // anchor's menu on the pointer alone. Its EXIT half is deliberately not
        // here — it runs at the top of this function, where every branch is still
        // ahead of it. This call runs BEFORE the recompute so an open it
        // performs is already standing when the faces resolve — toggle_dropdown
        // clears them, and the recompute then re-derives the whole roster false
        // under the new popup, which is the correct answer for a pointer the
        // popup has taken.
        open_menu_row_anchor_on_hover(mouse_x, mouse_y);
        recompute_redesign_button_hover();
        return;
    }
    // Left button must still be held down — otherwise release was lost.
    if (!mods.primary_button_held) {
        marker_drag.commit_drag();
        return;
    }
    const int sr = audio.sample_rate();
    if (sr <= 0) return;
    const GuiRect area = waveform_area(app);
    const double spp = current_samples_per_pixel(app, audio);
    // The delta handed to apply_drag_motion is an ACTIVE-domain frame delta:
    // mouse_frame is the pointer's plain active-domain position — one
    // expression, both views, no inverse map anywhere in its derivation.
    // The displayed-map hops that carry the delta into the source domain
    // live inside apply_drag_motion, which anchors the proposal in the
    // DISPLAYED target domain so the painted flag tracks the pointer 1:1.
    const double mouse_frame = static_cast<double>(app.viewport_start_sample) +
        static_cast<double>(mouse_x - area.x) * spp;
    marker_drag.apply_drag_motion(mouse_frame - app.drag.anchor_mouse_time_frame);
    // Playhead rule: the playhead follows the dragged marker through the drag
    // inside apply_drag_motion (the arming click landed it on the marker, so
    // the drag tows it by construction — the DragState ruling). The selection
    // re-assert already ran at the THRESHOLD CROSSING in begin_drag — a no-op,
    // the arming press having single-selected the marker, and unconditional so a
    // wall-saturated drag still names what it grabbed. apply_drag_motion here only
    // writes the proposal and slides the playhead. Nothing further tracks here.
}
